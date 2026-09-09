#include "ScanEngine.h"

#include "DirHandle.h"
#include "ReflinkAnalyzer.h"
#include "VisitedSet.h"
#include "WorkQueue.h"
#include "../attrs/DirectoryReader.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ads {

namespace {

    QString canonicalize(const QString &path)
    {
        QString expanded = path;
        if (expanded.startsWith(u'~'))
            expanded = QString::fromLocal8Bit(qgetenv("HOME")) + expanded.mid(1);
        const QByteArray native = QFile::encodeName(expanded);
        char buffer[PATH_MAX];
        if (realpath(native.constData(), buffer))
            return QFile::decodeName(buffer);
        return expanded;
    }

    void raiseFileDescriptorLimit()
    {
        rlimit limits {};
        if (getrlimit(RLIMIT_NOFILE, &limits) != 0)
            return;
        constexpr rlim_t target = 32768;
        if (limits.rlim_cur < target) {
            limits.rlim_cur = std::min(target, limits.rlim_max);
            setrlimit(RLIMIT_NOFILE, &limits);
        }
    }

} // namespace

struct ScanSession::Private {
    WorkQueue queue;
    GraphBuilder builder;
    VisitedSet visited;
    QSet<quint64> allowedDevices;
    ScanOptions options;
    QString canonicalRoot;
    QAtomicInt cancelled{0};
    QAtomicInt finished{0};
    /// Number of worker threads that have exited run(); reaches workerCount
    /// when the traversal is fully done. QThread::wait() races with start(),
    /// so completion is tracked explicitly.
    QAtomicInt workersDone{0};
    int workerCount = 0;
    QMutex doneMutex;
    QWaitCondition doneCond;
    std::vector<std::unique_ptr<QThread>> workers;
    QElapsedTimer clock;
};

ScanSession::ScanSession(std::unique_ptr<Private> priv)
    : d(std::move(priv))
{
}

ScanSession::~ScanSession()
{
    cancel();
    wait();
}

void ScanSession::cancel()
{
    // testAndSetRelaxed returns true on success (was 0, now 1): only the
    // first caller cancels the queue.
    if (!d->cancelled.testAndSetRelaxed(0, 1))
        return;
    d->queue.cancelAll();
}

ScanProgress ScanSession::progress() const
{
    ScanProgress p = d->builder.progress();
    p.finished = isFinished();
    return p;
}

bool ScanSession::isFinished() const
{
    // Worker completion == traversal done; the graph is finalized by wait().
    return d->workersDone.loadRelaxed() >= d->workerCount;
}

namespace {

    /// Worker loop: pull directories, enumerate, stream entries to the builder.
    void processDirectory(ScanSession::Private *ctx, DirWork &work)
    {
        int fd;
        if (work.parent) {
            fd = openat(work.parent->fd, work.name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            work.parent->release();
            work.parent = nullptr;
        } else {
            fd = open(QFile::encodeName(work.path).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        }
        if (fd < 0) {
            const int code = errno;
            if (code == EPERM || code == EACCES)
                ctx->builder.markDenied(work.nodeID, work.path, code);
            ctx->builder.dirFinished(work.path);
            return;
        }

        struct stat st {};
        if (fstat(fd, &st) != 0) {
            close(fd);
            ctx->builder.dirFinished(work.path);
            return;
        }
        const quint64 device = quint64(st.st_dev);

        if (!ctx->allowedDevices.contains(device) && !ctx->options.crossVolumes) {
            close(fd);
            ctx->builder.markOtherVolume(work.nodeID);
            ctx->builder.dirFinished(work.path);
            return;
        }
        if (!ctx->visited.markVisited(device, quint64(st.st_ino))) {
            close(fd);
            ctx->builder.markDuplicate(work.nodeID);
            ctx->builder.dirFinished(work.path);
            return;
        }

        DirHandle *handle = DirHandle::create(fd);
        DirectoryReader reader(fd);

        while (!ctx->cancelled.loadRelaxed()) {
            int error = 0;
            const QVector<RawEntry> batch = reader.nextBatch(&error);
            if (error != 0) {
                if (error == EPERM || error == EACCES)
                    ctx->builder.markDenied(work.nodeID, work.path, error);
                break;
            }
            if (batch.isEmpty())
                break;

            const QVector<qint32> ids = ctx->builder.addChildren(work.nodeID, device, batch);

            QVector<DirWork> subdirectories;
            for (qsizetype i = 0; i < batch.size(); ++i) {
                if (!batch[i].isDirectory())
                    continue;
                const QString childPath =
                    work.path == u"/" ? u"/" + QString::fromUtf8(batch[i].name)
                                      : work.path + u'/' + QString::fromUtf8(batch[i].name);
                handle->retain();
                subdirectories.append(
                    DirWork{ids[i], handle, batch[i].name, childPath});
            }
            if (!subdirectories.isEmpty())
                ctx->queue.push(subdirectories);
        }

        handle->release();
        ctx->builder.dirFinished(work.path);
    }

} // namespace

ScanResult ScanSession::wait()
{
    // Block until every worker has exited. QThread::wait()/isRunning() race
    // with start() (a just-started thread can report not-running), so
    // completion is tracked via an explicit counter + condition.
    {
        QMutexLocker lock(&d->doneMutex);
        while (d->workersDone.loadRelaxed() < d->workerCount)
            d->doneCond.wait(&d->doneMutex);
    }
    for (auto &t : d->workers)
        if (t)
            t->wait();
    d->workers.clear();

    // NB: QAtomicInt::testAndSetRelaxed returns true on SUCCESS (value was
    // 0 and is now 1) — not the old value. First caller finalizes; later
    // callers get an empty result.
    if (!d->finished.testAndSetRelaxed(0, 1))
        return {}; // already consumed

    const double wall = d->clock.nsecsElapsed() / 1e9;
    ScanResult result;
    result.graph = d->builder.finalize(d->canonicalRoot, d->cancelled.loadRelaxed() != 0, wall);
    // Optional reflink pass: honest freeable sizes on CoW filesystems.
    if (d->options.detectReflinks && !result.graph.summary.partial)
        result.graph = ReflinkAnalyzer::analyze(result.graph);
    return result;
}

std::unique_ptr<ScanSession> ScanEngine::scan(const QString &path, const ScanOptions &options,
                                              QString *error)
{
    const QString canonical = canonicalize(path);

    const int probeFD = open(QFile::encodeName(canonical).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (probeFD < 0) {
        if (error) {
            *error = errno == ENOTDIR
                ? QStringLiteral("Not a directory: %1").arg(canonical)
                : QStringLiteral("Cannot open %1: %2").arg(canonical, QString::fromUtf8(strerror(errno)));
        }
        return nullptr;
    }
    struct stat rootStat {};
    fstat(probeFD, &rootStat);
    close(probeFD);

    raiseFileDescriptorLimit();

    auto priv = std::make_unique<ScanSession::Private>();
    priv->options = options;
    priv->canonicalRoot = canonical;
    priv->allowedDevices.insert(quint64(rootStat.st_dev));

    auto session = std::unique_ptr<ScanSession>(new ScanSession(std::move(priv)));
    ScanSession::Private *ctx = session->d.get();

    ctx->clock.start();
    const qint32 rootID = ctx->builder.addRoot(canonical);
    ctx->queue.push(QVector<DirWork>{DirWork{rootID, nullptr, QFile::encodeName(canonical), canonical}});

    const int workerCount = std::max(options.maxConcurrency, 1);
    ctx->workerCount = workerCount;
    ctx->workers.reserve(workerCount);
    for (int i = 0; i < workerCount; ++i) {
        // A QThread whose run() IS the worker loop (subclassing is the correct
        // pattern here; connecting to started() would run the slot in the
        // caller's thread without a moved QObject).
        class WorkerThread : public QThread
        {
        public:
            explicit WorkerThread(ScanSession::Private *ctx)
                : m_ctx(ctx)
            {
            }

        protected:
            void run() override
            {
                DirWork work;
                while (!m_ctx->cancelled.loadRelaxed() && m_ctx->queue.next(work)) {
                    processDirectory(m_ctx, work);
                    m_ctx->queue.complete();
                }
                m_ctx->workersDone.fetchAndAddRelaxed(1);
                QMutexLocker lock(&m_ctx->doneMutex);
                m_ctx->doneCond.wakeAll();
            }

        private:
            ScanSession::Private *m_ctx;
        };

        auto thread = std::make_unique<WorkerThread>(ctx);
        thread->setObjectName(QStringLiteral("ads-scan-%1").arg(i));
        thread->start();
        ctx->workers.push_back(std::move(thread));
    }

    return session;
}

} // namespace ads
