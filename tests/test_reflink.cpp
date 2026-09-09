#include <QTest>

#include "scan/ScanEngine.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include <stdlib.h> // mkdtemp
#include <unistd.h>

using namespace ads;

// Reflink (cp --reflink=always) fixture: two clones of one file must share
// physical bytes, charged once at the LCA. Runs only where the fixture FS
// supports reflinks (the CI/dev btrfs home); skipped elsewhere.
class TestReflink : public QObject
{
    Q_OBJECT

private:
    static bool reflinkCopy(const QString &src, const QString &dst)
    {
        QProcess p;
        p.start(QStringLiteral("cp"),
                {QStringLiteral("--reflink=always"), src, dst});
        return p.waitForFinished(5000) && p.exitCode() == 0;
    }

    static ScanResult scanSync(const QString &path)
    {
        ScanOptions opts;
        opts.detectReflinks = true;
        QString error;
        auto session = ScanEngine::scan(path, opts, &error);
        if (!session)
            return ScanResult{FileGraph{}, error};
        return session->wait();
    }

private:
    // Fixture root on a reflink-capable filesystem. /tmp is tmpfs on Fedora
    // Asahi, so use a dir under HOME (btrfs); fall back to /tmp elsewhere.
    static QString makeFixtureRoot()
    {
        const QString home = QDir::homePath();
        const QString base = home + QStringLiteral("/.ads-reflink-test-XXXXXX");
        // mkdtemp for uniqueness.
        QByteArray tmpl = QFile::encodeName(base);
        if (::mkdtemp(tmpl.data()))
            return QFile::decodeName(tmpl);
        return {};
    }

private slots:
    void reflinkClonesShareOnce()
    {
        const QString basePath = makeFixtureRoot();
        QVERIFY(!basePath.isEmpty());
        struct Cleanup {
            QString p;
            ~Cleanup() { QDir(p).removeRecursively(); }
        } cleanup{basePath};

        // Detect reflink support by trying one copy.
        QVERIFY(writeFile(basePath + QStringLiteral("/orig.bin"), 4 * 1024 * 1024));
        if (!reflinkCopy(basePath + QStringLiteral("/orig.bin"),
                         basePath + QStringLiteral("/clone.bin")))
            QSKIP("filesystem does not support reflink copies");

        const ScanResult result = scanSync(basePath);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const FileGraph &g = result.graph;

        // Both clones detected as one family.
        QCOMPARE(g.summary.cloneFileCount, 2);

        // Physical total must be ~one copy, not two. The kernel reports both
        // files at stx_blocks; the analyzer subtracts the duplicate.
        const qint64 logical = g.summary.totalLogical;
        const qint64 physical = g.summary.totalPhysical;
        QVERIFY(physical < logical); // shared → less than 2 copies
        // Within a small tolerance of a single 4 MiB file's physical size.
        QVERIFY2(physical >= 4 * 1024 * 1024 && physical < 5 * 1024 * 1024,
                 qPrintable(QStringLiteral("physical=%1").arg(physical)));

        // Each clone individually frees ~0 (all bytes shared).
        for (const NodeID kid : g.children(g.root())) {
            const Sizes s = g.sizes(kid);
            QVERIFY2(s.unique < 1024 * 1024,
                     qPrintable(QStringLiteral("%1 unique=%2").arg(g.name(kid)).arg(s.unique)));
            QVERIFY(g.flags(kid).cloned());
        }
    }

    void untouchedFileKeepsFreeable()
    {
        const QString basePath = makeFixtureRoot();
        QVERIFY(!basePath.isEmpty());
        struct Cleanup {
            QString p;
            ~Cleanup() { QDir(p).removeRecursively(); }
        } cleanup{basePath};
        QVERIFY(writeFile(basePath + QStringLiteral("/solo.bin"), 1024 * 1024));

        const ScanResult result = scanSync(basePath);
        if (!result.error.isEmpty())
            QSKIP("scan failed");
        const FileGraph &g = result.graph;
        // No reflink sharing → freeable == physical for the single file.
        for (const NodeID kid : g.children(g.root())) {
            if (g.name(kid) == u"solo.bin") {
                QVERIFY(!g.flags(kid).cloned());
                QCOMPARE(g.sizes(kid).unique, g.sizes(kid).physical);
            }
        }
    }

private:
    static bool writeFile(const QString &path, qint64 size)
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        QByteArray chunk(65536, 'x');
        while (size > 0) {
            const qint64 n = std::min(size, qint64(chunk.size()));
            if (f.write(chunk.constData(), n) != n)
                return false;
            size -= n;
        }
        return true;
    }
};

QTEST_GUILESS_MAIN(TestReflink)
#include "test_reflink.moc"
