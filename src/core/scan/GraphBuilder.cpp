#include "GraphBuilder.h"

#include <QMutexLocker>

#include <algorithm>

namespace ads {

GraphBuilder::GraphBuilder()
{
    m_nameOffsets.reserve(4096);
    m_parents.reserve(4096);
}

qint32 GraphBuilder::addRoot(const QString &name)
{
    QMutexLocker lock(&m_mutex);
    return appendNode(name.toUtf8(), -1, 0, NodeFlags(NodeKind::Directory), 0, 0, 0);
}

QVector<qint32> GraphBuilder::addChildren(qint32 parent, quint64 device,
                                          const QVector<RawEntry> &entries)
{
    QMutexLocker lock(&m_mutex);
    QVector<qint32> ids;
    ids.reserve(entries.size());
    const quint16 childDepth = m_depths[parent] + 1;

    for (const RawEntry &entry : entries) {
        const NodeKind kind = entry.isDirectory() ? NodeKind::Directory
            : entry.isSymlink()                   ? NodeKind::Symlink
            : entry.isRegularFile()               ? NodeKind::File
                                                  : NodeKind::Other;
        NodeFlags flags(kind);

        qint64 logical = 0;
        qint64 physical = 0;
        qint64 unique = 0;

        if (kind != NodeKind::Directory) {
            logical = std::max(entry.logical, qint64(0));
            physical = std::max(entry.physical, qint64(0));
            // Phase 1: no PRIVATESIZE equivalent — per-file freeable is the
            // file's own physical bytes (exact for unshared files).
            unique = physical;

            if (entry.isCompressed())
                flags.setCompressed(true);
            if (entry.isSparse()) {
                flags.setSparse(true);
                ++m_summary.sparseFileCount;
            }

            if (entry.linkCount > 1 && kind == NodeKind::File) {
                // Deleting one of several hard links frees nothing.
                unique = 0;
                flags.setHardlinked(true);
                ++m_summary.hardlinkedFileCount;
            }

            if (kind == NodeKind::Symlink)
                ++m_summary.symlinkCount;
            else
                ++m_summary.fileCount;
            m_progressLogical += logical;
            m_progressPhysical += physical;
        } else {
            ++m_summary.directoryCount;
        }

        const qint32 id = appendNode(entry.name, parent, childDepth, flags, logical, physical, unique);
        ids.append(id);

        if (kind == NodeKind::File && entry.linkCount > 1) {
            HardlinkAcc &acc = m_hardlinks[HardlinkKey{device, entry.fileID}];
            acc.nodes.append(id);
            acc.totalLinks = std::max(acc.totalLinks, entry.linkCount);
            acc.physical = std::max(acc.physical, physical);
        }
    }
    return ids;
}

qint32 GraphBuilder::appendNode(const QByteArray &name, qint32 parent, quint16 depth,
                                NodeFlags flags, qint64 logical, qint64 physical, qint64 unique)
{
    const qint32 id = m_parents.size();
    m_nameOffsets.append(quint32(m_nameArena.size()));
    m_nameLengths.append(quint16(std::min(qsizetype(name.size()), qsizetype(UINT16_MAX))));
    m_nameArena.append(name.constData(), m_nameLengths.last());
    m_parents.append(parent);
    m_depths.append(depth);
    m_flagsArr.append(flags.rawValue());
    m_logicalArr.append(logical);
    m_physicalArr.append(physical);
    m_uniqueArr.append(unique);
    return id;
}

void GraphBuilder::dirFinished(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    ++m_directoriesScanned;
    m_lastPath = path;
}

void GraphBuilder::markDenied(qint32 node, const QString &path, qint32 code)
{
    QMutexLocker lock(&m_mutex);
    m_ledger.record(path, code);
    NodeFlags flags(m_flagsArr[node]);
    flags.setAccessDenied(true);
    m_flagsArr[node] = flags.rawValue();
}

void GraphBuilder::markOtherVolume(qint32 node)
{
    QMutexLocker lock(&m_mutex);
    NodeFlags flags(m_flagsArr[node]);
    flags.setOtherVolume(true);
    m_flagsArr[node] = flags.rawValue();
}

void GraphBuilder::markDuplicate(qint32 node)
{
    QMutexLocker lock(&m_mutex);
    NodeFlags flags(m_flagsArr[node]);
    flags.setDuplicate(true);
    m_flagsArr[node] = flags.rawValue();
}

ScanProgress GraphBuilder::progress() const
{
    QMutexLocker lock(&m_mutex);
    return ScanProgress{m_directoriesScanned,
                        m_summary.fileCount + m_summary.symlinkCount,
                        m_progressLogical,
                        m_progressPhysical,
                        qint64(m_ledger.deniedCount),
                        m_lastPath,
                        false};
}

FileGraph GraphBuilder::finalize(const QString &rootPath, bool partial, double wallSeconds)
{
    QMutexLocker lock(&m_mutex);
    const qsizetype n = m_parents.size();

    // Children in CSR form.
    QVector<qint32> childCount(n, 0);
    for (qsizetype i = 1; i < n; ++i)
        ++childCount[m_parents[i]];
    QVector<qint32> childStart(n + 1, 0);
    for (qsizetype i = 0; i < n; ++i)
        childStart[i + 1] = childStart[i] + childCount[i];
    QVector<qint32> insertion(childStart.constBegin(), childStart.constBegin() + n);
    QVector<qint32> childItems(std::max(n - 1, qsizetype(0)), 0);
    for (qsizetype i = 1; i < n; ++i) {
        const qint32 p = m_parents[i];
        childItems[insertion[p]++] = qint32(i);
    }

    // Shared-bytes corrections charged at group LCAs.
    QHash<qint32, qint64> physicalAdjust;
    QHash<qint32, qint64> uniqueAdjust;

    for (const HardlinkAcc &acc : std::as_const(m_hardlinks)) {
        const qsizetype seen = acc.nodes.size();
        if (seen < 1)
            continue;
        const qint32 anchor = lowestCommonAncestor(acc.nodes);
        if (seen > 1)
            // Rollups above the LCA must count this inode's bytes once, not
            // `seen` times.
            physicalAdjust[anchor] -= qint64(seen - 1) * acc.physical;
        if (quint32(seen) == acc.totalLinks) {
            // Every link is inside this subtree: deleting it all really frees
            // the content.
            uniqueAdjust[anchor] += acc.physical;
        } else {
            for (qint32 node : acc.nodes) {
                NodeFlags flags(m_flagsArr[node]);
                flags.setExternalLinks(true);
                m_flagsArr[node] = flags.rawValue();
            }
        }
    }

    // Bottom-up rollup. Nodes are appended parent-before-child, so reverse
    // index order visits every child before its parent.
    for (qsizetype i = n - 1; i >= 1; --i) {
        const qint32 id = qint32(i);
        physicalAdjust.value(id);
        if (const auto it = physicalAdjust.constFind(id); it != physicalAdjust.constEnd())
            m_physicalArr[i] += it.value();
        if (const auto it = uniqueAdjust.constFind(id); it != uniqueAdjust.constEnd())
            m_uniqueArr[i] += it.value();
        const qint32 p = m_parents[i];
        m_logicalArr[p] += m_logicalArr[i];
        m_physicalArr[p] += m_physicalArr[i];
        m_uniqueArr[p] += m_uniqueArr[i];
    }
    if (n > 0) {
        if (const auto it = physicalAdjust.constFind(0); it != physicalAdjust.constEnd())
            m_physicalArr[0] += it.value();
        if (const auto it = uniqueAdjust.constFind(0); it != uniqueAdjust.constEnd())
            m_uniqueArr[0] += it.value();
    }

    // Largest-first children for every consumer of the graph.
    for (qsizetype i = 0; i < n; ++i) {
        if (childCount[i] > 1) {
            std::sort(childItems.begin() + childStart[i], childItems.begin() + childStart[i + 1],
                      [&](qint32 a, qint32 b) { return m_physicalArr[a] > m_physicalArr[b]; });
        }
    }

    m_summary.deniedDirectoryCount = m_ledger.deniedCount;
    m_summary.partial = partial;
    m_summary.wallSeconds = wallSeconds;
    if (n > 0) {
        m_summary.totalLogical = m_logicalArr[0];
        m_summary.totalPhysical = m_physicalArr[0];
        m_summary.totalUnique = m_uniqueArr[0];
    }

    FileGraph graph;
    graph.nameArena = std::move(m_nameArena);
    graph.nameOffsets = std::move(m_nameOffsets);
    graph.nameLengths = std::move(m_nameLengths);
    graph.parents = std::move(m_parents);
    graph.depths = std::move(m_depths);
    graph.logicalArr = std::move(m_logicalArr);
    graph.physicalArr = std::move(m_physicalArr);
    graph.uniqueArr = std::move(m_uniqueArr);
    graph.flagsArr = std::move(m_flagsArr);
    graph.childStart = std::move(childStart);
    graph.childItems = std::move(childItems);
    graph.rootPath = rootPath;
    graph.ledger = std::move(m_ledger);
    graph.summary = m_summary;
    return graph;
}

qint32 GraphBuilder::lowestCommonAncestor(const QVector<qint32> &nodes) const
{
    if (nodes.isEmpty())
        return 0;
    qint32 current = nodes.first();
    for (qsizetype i = 1; i < nodes.size(); ++i) {
        current = lowestCommonAncestor(current, nodes[i]);
        if (current == 0)
            break;
    }
    return current;
}

qint32 GraphBuilder::lowestCommonAncestor(qint32 a, qint32 b) const
{
    qint32 x = a;
    qint32 y = b;
    while (m_depths[x] > m_depths[y])
        x = m_parents[x];
    while (m_depths[y] > m_depths[x])
        y = m_parents[y];
    while (x != y) {
        x = m_parents[x];
        y = m_parents[y];
    }
    return x;
}

} // namespace ads
