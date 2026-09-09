#include "ScanController.h"

#include "../core/util/ByteFormat.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QProcess>
#include <QUrl>

namespace ads {

ScanController::ScanController(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(100);
    connect(&m_pollTimer, &QTimer::timeout, this, &ScanController::pollProgress);
}

ScanController::~ScanController() = default;

QString ScanController::rootPath() const
{
    return m_graph ? m_graph->rootPath : QString();
}

void ScanController::setLens(int lens)
{
    const SizeLens l = SizeLens(lens);
    if (l == m_lens)
        return;
    m_lens = l;
    emit lensChanged();
}

void ScanController::setFocusNode(qint32 raw)
{
    if (!m_graph || raw < 0 || raw >= m_graph->nodeCount())
        return;
    if (m_focus.raw == raw)
        return;
    m_focus = NodeID{raw};
    emit focusChanged();
}

void ScanController::setSelectedNode(qint32 raw)
{
    if (m_selected.raw == raw)
        return;
    m_selected = NodeID{raw};
    emit selectionChanged();
}

QStringList ScanController::breadcrumbs() const
{
    QStringList out;
    if (!m_graph)
        return out;
    NodeID cur = m_focus;
    while (cur.raw >= 0) {
        out.prepend(cur.raw == 0 ? m_graph->rootPath : m_graph->name(cur));
        cur = m_graph->parent(cur);
    }
    return out;
}

void ScanController::scan(const QString &path)
{
    cancelScan();
    m_graph.reset();

    // Always run the reflink pass in the GUI so the Freeable lens is honest
    // on CoW filesystems. The graph is finalized before the results page
    // appears, so the extra pass overlaps with page setup.
    ScanOptions options;
    options.detectReflinks = true;

    QString error;
    m_session = ScanEngine::scan(path, options, &error);
    if (!m_session) {
        emit errorOccurred(error);
        return;
    }
    m_state = Scanning;
    emit stateChanged();
    m_pollTimer.start();
}

void ScanController::pickAndScan()
{
    const QString dir = QFileDialog::getExistingDirectory(
        nullptr, QStringLiteral("Choose folder to scan"),
        QStringLiteral("/home"), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty())
        scan(dir);
}

void ScanController::cancelScan()
{
    if (m_session) {
        m_pollTimer.stop();
        m_session->cancel();
        m_session->wait(); // finalize partial
        m_session.reset();
    }
}

void ScanController::reset()
{
    cancelScan();
    m_graph.reset();
    m_state = Welcome;
    m_focus = NodeID{0};
    m_selected = NodeID{0};
    emit stateChanged();
    emit graphChanged();
}

void ScanController::pollProgress()
{
    if (!m_session)
        return;
    m_progress = m_session->progress();
    emit progressChanged();
    if (m_session->isFinished())
        finishScan();
}

void ScanController::finishScan()
{
    m_pollTimer.stop();
    ScanResult result = m_session->wait();
    m_session.reset();
    m_graph = std::move(result.graph);
    m_focus = NodeID{0};
    m_selected = NodeID{0};
    m_state = Results;
    emit stateChanged();
    emit graphChanged();
    emit focusChanged();
}

QString ScanController::nodeName(qint32 raw) const
{
    return m_graph && raw >= 0 && raw < m_graph->nodeCount() ? m_graph->name(NodeID{raw})
                                                             : QString();
}

QString ScanController::nodePath(qint32 raw) const
{
    return m_graph && raw >= 0 && raw < m_graph->nodeCount() ? m_graph->path(NodeID{raw})
                                                             : QString();
}

qint64 ScanController::nodeSize(qint32 raw) const
{
    return m_graph ? m_graph->size(NodeID{raw}, m_lens) : 0;
}

qint64 ScanController::nodeLogical(qint32 raw) const
{
    return m_graph ? m_graph->sizes(NodeID{raw}).logical : 0;
}

qint64 ScanController::nodePhysical(qint32 raw) const
{
    return m_graph ? m_graph->sizes(NodeID{raw}).physical : 0;
}

qint64 ScanController::nodeUnique(qint32 raw) const
{
    return m_graph ? m_graph->sizes(NodeID{raw}).unique : 0;
}

bool ScanController::nodeIsDir(qint32 raw) const
{
    return m_graph ? m_graph->kind(NodeID{raw}) == NodeKind::Directory : false;
}

QString ScanController::nodeBadges(qint32 raw) const
{
    if (!m_graph)
        return {};
    const NodeFlags f = m_graph->flags(NodeID{raw});
    QStringList parts;
    if (f.cloned())
        parts << QStringLiteral("⧉ clone");
    if (f.sparse())
        parts << QStringLiteral("▤ sparse");
    if (f.hardlinked())
        parts << QStringLiteral("⛓ hardlink");
    if (f.compressed())
        parts << QStringLiteral("▣ compressed");
    if (f.accessDenied())
        parts << QStringLiteral("⛔ unreadable");
    if (f.otherVolume())
        parts << QStringLiteral("⇥ other mount");
    if (f.duplicate())
        parts << QStringLiteral("↩ already counted");
    if (f.externalLinks())
        parts << QStringLiteral("✳ shared outside");
    return parts.join(QStringLiteral("   "));
}

QVector<qint32> ScanController::nodeChildren(qint32 raw) const
{
    QVector<qint32> out;
    if (!m_graph)
        return out;
    const QVector<NodeID> kids = m_graph->children(NodeID{raw});
    out.reserve(kids.size());
    for (const NodeID k : kids)
        out.append(k.raw);
    if (m_lens != SizeLens::Physical)
        std::sort(out.begin(), out.end(), [&](qint32 a, qint32 b) {
            return m_graph->size(NodeID{a}, m_lens) > m_graph->size(NodeID{b}, m_lens);
        });
    return out;
}

qint32 ScanController::nodeParent(qint32 raw) const
{
    if (!m_graph)
        return -1;
    return m_graph->parent(NodeID{raw}).raw;
}

QString ScanController::formatBytes(qint64 bytes) const
{
    return ads::formatBytes(bytes);
}

qint64 ScanController::totalFiles() const
{
    return m_graph ? m_graph->summary.fileCount : 0;
}

qint64 ScanController::totalDirs() const
{
    return m_graph ? m_graph->summary.directoryCount : 0;
}

qint64 ScanController::deniedCount() const
{
    return m_graph ? m_graph->summary.deniedDirectoryCount : 0;
}

double ScanController::wallSeconds() const
{
    return m_graph ? m_graph->summary.wallSeconds : 0.0;
}

void ScanController::revealInFileManager(qint32 raw) const
{
    if (!m_graph)
        return;
    const QString path = m_graph->path(NodeID{raw});
    // Opening the parent dir selects nothing portably; opening the path itself
    // works for files and folders alike in Dolphin.
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool ScanController::moveToTrash(qint32 raw)
{
    if (!m_graph || raw <= 0)
        return false;
    const QString path = m_graph->path(NodeID{raw});

    // FreeDesktop trash via gio (always present on a KDE system).
    QProcess proc;
    proc.start(QStringLiteral("gio"), {QStringLiteral("trash"), path});
    if (!proc.waitForFinished(10000) || proc.exitCode() != 0)
        return false;

    // Patch the graph in memory (O(depth) tombstone).
    m_graph = m_graph->removing(NodeID{raw});
    if (m_selected.raw == raw)
        m_selected = NodeID{0};
    emit graphChanged();
    emit selectionChanged();
    return true;
}

} // namespace ads
