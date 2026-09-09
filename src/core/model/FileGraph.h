#pragma once

#include "NodeFlags.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace ads {

/// Identifier of a node in a FileGraph. Raw index into the graph's arrays.
struct NodeID {
    qint32 raw = -1;
    bool operator==(const NodeID &) const = default;
};

/// The honest size lenses. Phase 1: Logical + Physical only. The Unique
/// (Freeable) lens returns from Phase 2 once btrfs reflink sharing is
/// measurable; until then unique mirrors physical-per-file minus hardlink
/// zeroing, and the UI exposes only Logical/Physical.
enum class SizeLens {
    Logical,
    Physical,
    Unique,
};

struct Sizes {
    qint64 logical = 0;
    qint64 physical = 0;
    qint64 unique = 0;

    qint64 operator[](SizeLens lens) const
    {
        switch (lens) {
        case SizeLens::Logical:
            return logical;
        case SizeLens::Physical:
            return physical;
        case SizeLens::Unique:
            return unique;
        }
        return 0;
    }

    bool operator==(const Sizes &) const = default;
};

/// Directories the scan could not read — recorded, never silently skipped.
struct PermissionLedger {
    struct DeniedEntry {
        QString path;
        qint32 code = 0;
    };

    /// First N denials with full paths (capped to bound memory).
    QVector<DeniedEntry> denied;
    int deniedCount = 0;
    static constexpr int maxRecorded = 2000;

    void record(const QString &path, qint32 code)
    {
        ++deniedCount;
        if (denied.size() < maxRecorded)
            denied.append({path, code});
    }
};

/// Headline numbers for a completed scan.
struct ScanSummary {
    qint64 fileCount = 0;
    qint64 directoryCount = 0;
    qint64 symlinkCount = 0;
    qint64 totalLogical = 0;
    qint64 totalPhysical = 0;
    qint64 totalUnique = 0;
    qint64 cloneFileCount = 0;
    qint64 hardlinkedFileCount = 0;
    qint64 sparseFileCount = 0;
    qint64 deniedDirectoryCount = 0;
    bool partial = false;
    double wallSeconds = 0.0;
};

/// Immutable scan result: the whole tree in struct-of-arrays form.
/// ~46 bytes per node plus name bytes — millions of files fit comfortably.
///
/// All arrays are public-by-design within the library: GraphBuilder fills them,
/// FileGraph exposes read-only accessors, `removing()` produces an edited copy.
struct FileGraph {
    // Hot parallel arrays, indexed by NodeID.raw.
    QByteArray nameArena;
    QVector<quint32> nameOffsets;
    QVector<quint16> nameLengths;
    QVector<qint32> parents; // -1 for root
    QVector<quint16> depths;
    QVector<qint64> logicalArr;
    QVector<qint64> physicalArr;
    QVector<qint64> uniqueArr;
    QVector<quint16> flagsArr;
    // CSR children layout: children(of: i) = childItems[childStart[i]..childStart[i+1])
    QVector<qint32> childStart;
    QVector<qint32> childItems;

    QString rootPath;
    PermissionLedger ledger;
    ScanSummary summary;

    NodeID root() const { return NodeID{0}; }
    qsizetype nodeCount() const { return parents.size(); }

    QString name(NodeID node) const
    {
        const qsizetype i = node.raw;
        return QString::fromUtf8(nameArena.constData() + nameOffsets[i], nameLengths[i]);
    }

    NodeID parent(NodeID node) const
    {
        const qint32 p = parents[node.raw];
        return p >= 0 ? NodeID{p} : NodeID{-1};
    }

    int depth(NodeID node) const { return depths[node.raw]; }
    NodeFlags flags(NodeID node) const { return NodeFlags(flagsArr[node.raw]); }
    NodeKind kind(NodeID node) const { return flags(node).kind(); }

    Sizes sizes(NodeID node) const
    {
        const qsizetype i = node.raw;
        return Sizes{logicalArr[i], physicalArr[i], uniqueArr[i]};
    }

    qint64 size(NodeID node, SizeLens lens) const { return sizes(node)[lens]; }

    /// Children, sorted by physical size descending. Tombstoned entries
    /// (deleted via `removing()`) are skipped.
    QVector<NodeID> children(NodeID node) const
    {
        const qsizetype i = node.raw;
        QVector<NodeID> out;
        out.reserve(childStart[i + 1] - childStart[i]);
        for (qint32 k = childStart[i]; k < childStart[i + 1]; ++k)
            if (childItems[k] >= 0)
                out.append(NodeID{childItems[k]});
        return out;
    }

    int childCount(NodeID node) const { return int(children(node).size()); }

    /// Absolute path of a node (root carries the full scan-root path).
    QString path(NodeID node) const
    {
        QStringList components;
        NodeID current = node;
        while (current.raw >= 0) {
            const NodeID p = parent(current);
            if (p.raw < 0)
                break;
            components.prepend(name(current));
            current = p;
        }
        QString path = rootPath;
        if (path.endsWith(u'/') && path.size() > 1)
            path.chop(1);
        for (const QString &c : components)
            path += u'/' + c;
        return path.isEmpty() ? QStringLiteral("/") : path;
    }

    /// The graph after `node` is deleted (e.g. moved to Trash): its rolled-up
    /// sizes are subtracted from every ancestor and it disappears from its
    /// parent's children via a tombstone. O(depth); no reindexing. A rescan
    /// refreshes exact shared-bytes accounting.
    FileGraph removing(NodeID node) const
    {
        if (node.raw <= 0)
            return *this;
        FileGraph copy = *this;
        const Sizes removed = sizes(node);

        qint32 ancestor = parents[node.raw];
        while (ancestor >= 0) {
            copy.logicalArr[ancestor] -= removed.logical;
            copy.physicalArr[ancestor] -= removed.physical;
            copy.uniqueArr[ancestor] -= removed.unique;
            ancestor = parents[ancestor];
        }

        copy.logicalArr[node.raw] = 0;
        copy.physicalArr[node.raw] = 0;
        copy.uniqueArr[node.raw] = 0;

        const qint32 p = parents[node.raw];
        if (p >= 0)
            for (qint32 k = copy.childStart[p]; k < copy.childStart[p + 1]; ++k)
                if (copy.childItems[k] == node.raw)
                    copy.childItems[k] = -1;

        copy.summary.totalLogical = copy.logicalArr[0];
        copy.summary.totalPhysical = copy.physicalArr[0];
        copy.summary.totalUnique = copy.uniqueArr[0];
        return copy;
    }
};

} // namespace ads
