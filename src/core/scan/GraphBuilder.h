#pragma once

#include "../attrs/RawEntry.h"
#include "../model/FileGraph.h"

#include <QHash>
#include <QMutex>
#include <QSet>

namespace ads {

struct ScanProgress {
    qint64 directoriesScanned = 0;
    qint64 filesSeen = 0;
    qint64 logicalBytes = 0;
    qint64 physicalBytes = 0;
    qint64 deniedCount = 0;
    QString currentPath;
    bool finished = false;
};

/// Single-writer accumulator for the scan: workers stream batches of parsed
/// entries in; it appends to compact struct-of-arrays storage, feeds the
/// hardlink registry, and at the end resolves shared-bytes accounting and
/// rolls sizes up the tree. All public methods are thread-safe (mutex) —
/// this is the original actor's exact role.
///
/// Accounting model ("shared bytes are counted once, in the deepest folder
/// that contains all files sharing them"):
/// - Per-file values are individual truth: physical = the file's own allocated
///   bytes; unique = what deleting that one path frees (0 for hardlinks with
///   other links).
/// - Directory rollups would multi-count shared extents, so corrections are
///   charged at the lowest common ancestor (LCA) of each sharing group:
///   duplicate hardlink bytes subtracted from physical; freed-if-all-deleted
///   bytes added to unique when the whole group lies inside the tree.
///
/// Phase 1: hardlinks only. The clone registry lands with the Phase 2 btrfs
/// reflink engine — the LCA math below was built for exactly that.
class GraphBuilder
{
public:
    GraphBuilder();

    qint32 addRoot(const QString &name);

    /// Append one batch of a directory's entries; returns the node IDs
    /// assigned to each entry, in order.
    QVector<qint32> addChildren(qint32 parent, quint64 device, const QVector<RawEntry> &entries);

    void dirFinished(const QString &path);
    void markDenied(qint32 node, const QString &path, qint32 code);
    void markOtherVolume(qint32 node);
    void markDuplicate(qint32 node);

    ScanProgress progress() const;

    FileGraph finalize(const QString &rootPath, bool partial, double wallSeconds);

private:
    qint32 appendNode(const QByteArray &name, qint32 parent, quint16 depth, NodeFlags flags,
                      qint64 logical, qint64 physical, qint64 unique);
    qint32 lowestCommonAncestor(const QVector<qint32> &nodes) const;
    qint32 lowestCommonAncestor(qint32 a, qint32 b) const;

    // Struct-of-arrays node storage.
    QByteArray m_nameArena;
    QVector<quint32> m_nameOffsets;
    QVector<quint16> m_nameLengths;
    QVector<qint32> m_parents;
    QVector<quint16> m_depths;
    QVector<qint64> m_logicalArr;
    QVector<qint64> m_physicalArr;
    QVector<qint64> m_uniqueArr;
    QVector<quint16> m_flagsArr;

    // Sharing registry, consumed at finalize.
    struct HardlinkKey {
        quint64 device;
        quint64 fileID;
        bool operator==(const HardlinkKey &) const = default;
    };
    struct HardlinkAcc {
        QVector<qint32> nodes;
        quint32 totalLinks = 0;
        qint64 physical = 0;
    };
    friend size_t qHash(const HardlinkKey &key, size_t seed) noexcept;
    QHash<HardlinkKey, HardlinkAcc> m_hardlinks;

    PermissionLedger m_ledger;
    ScanSummary m_summary;
    qint64 m_directoriesScanned = 0;
    qint64 m_progressLogical = 0;
    qint64 m_progressPhysical = 0;
    QString m_lastPath;

    mutable QMutex m_mutex;
};

inline size_t qHash(const GraphBuilder::HardlinkKey &key, size_t seed) noexcept
{
    return qHashMulti(seed, key.device, key.fileID);
}

} // namespace ads
