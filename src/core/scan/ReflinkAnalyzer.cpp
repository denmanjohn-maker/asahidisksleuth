#include "ReflinkAnalyzer.h"

#include "../attrs/ExtentMap.h"
#include "../volume/VolumeInfo.h"

#include <fcntl.h>
#include <unistd.h>

#include <QFile>
#include <QHash>

#include <algorithm>

namespace ads::ReflinkAnalyzer {

namespace {

    /// Union-find over node ids to group files into clone families.
    struct UnionFind {
        explicit UnionFind(qsizetype n)
            : parent(n)
        {
            for (qsizetype i = 0; i < n; ++i)
                parent[i] = qint32(i);
        }
        qint32 find(qint32 x)
        {
            while (parent[x] != x) {
                parent[x] = parent[parent[x]];
                x = parent[x];
            }
            return x;
        }
        void unite(qint32 a, qint32 b)
        {
            a = find(a);
            b = find(b);
            if (a != b)
                parent[b] = a;
        }
        QVector<qint32> parent;
    };

    qint32 lca(const FileGraph &g, qint32 a, qint32 b)
    {
        qint32 x = a;
        qint32 y = b;
        while (g.depths[x] > g.depths[y])
            x = g.parents[x];
        while (g.depths[y] > g.depths[x])
            y = g.parents[y];
        while (x != y) {
            x = g.parents[x];
            y = g.parents[y];
        }
        return x;
    }

} // namespace

FileGraph analyze(const FileGraph &graph)
{
    // Only bother on CoW roots.
    VolumeInfo vol;
    if (!VolumeService::identity(graph.rootPath, &vol)
        || !ExtentMap::filesystemSupportsReflink(vol.fsType))
        return graph;

    const qsizetype n = graph.nodeCount();
    UnionFind uf(n);

    // Map each physical extent start to the first node that referenced it.
    // A collision means two files share that extent → same clone family.
    // Keyed by physical offset; values are node ids. (Extent granularity is
    // coarse — btrfs allocates in >=4 KiB runs — so start-offset collisions
    // are a reliable, cheap family signal.)
    QHash<quint64, qint32> extentOwner;
    // Per-node shared byte total (bytes in extents marked shared).
    QVector<qint64> sharedBytes(n, 0);
    QVector<bool> hasShared(n, false);

    for (qint32 raw = 0; raw < n; ++raw) {
        if (graph.kind(NodeID{raw}) != NodeKind::File)
            continue;

        const QString path = graph.path(NodeID{raw});
        const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0)
            continue;

        QVector<Extent> extents;
        const bool ok = ExtentMap::read(fd, extents);
        ::close(fd);
        if (!ok)
            continue;

        for (const Extent &e : extents) {
            if (!e.shared)
                continue;
            sharedBytes[raw] += qint64(e.length);
            hasShared[raw] = true;
            const auto it = extentOwner.constFind(e.physical);
            if (it == extentOwner.constEnd())
                extentOwner.insert(e.physical, raw);
            else
                uf.unite(it.value(), raw);
        }
    }

    // Any sharing at all?
    if (extentOwner.isEmpty())
        return graph;

    // Group families: root id -> member nodes.
    QHash<qint32, QVector<qint32>> families;
    for (qint32 raw = 0; raw < n; ++raw)
        if (hasShared[raw])
            families[uf.find(raw)].append(raw);

    FileGraph out = graph;

    // Per-file deltas (private bytes only); family LCA charges.
    QVector<qint64> physicalDelta(n, 0);
    QVector<qint64> uniqueDelta(n, 0);
    QHash<qint32, qint64> anchorCharge; // family shared bytes, at the LCA

    for (const QVector<qint32> &members : std::as_const(families)) {
        if (members.size() < 2)
            continue;

        qint64 familyShared = 0;
        for (qint32 m : members)
            familyShared = std::max(familyShared, sharedBytes[m]);
        if (familyShared <= 0)
            continue;

        qint32 anchor = members.first();
        for (qsizetype i = 1; i < members.size(); ++i)
            anchor = lca(graph, anchor, members[i]);

        // Files keep only their private bytes.
        for (qint32 m : members) {
            physicalDelta[m] -= sharedBytes[m];
            uniqueDelta[m] -= sharedBytes[m];
        }
        // The shared base is charged once at the LCA.
        anchorCharge[anchor] += familyShared;

        for (qint32 m : members) {
            NodeFlags flags(out.flagsArr[m]);
            flags.setCloned(true);
            flags.setSharedEstimate(true);
            out.flagsArr[m] = flags.rawValue();
        }
        out.summary.cloneFileCount += members.size();
    }

    // 1) Apply per-file deltas (files only; dirs are rebuilt below).
    for (qint32 raw = 0; raw < n; ++raw) {
        if (graph.kind(NodeID{raw}) != NodeKind::Directory) {
            out.physicalArr[raw] += physicalDelta[raw];
            out.uniqueArr[raw] += uniqueDelta[raw];
        }
    }

    // 2) Rebuild directory rollups from the corrected leaves.
    for (qint32 raw = 0; raw < n; ++raw) {
        if (graph.kind(NodeID{raw}) == NodeKind::Directory) {
            out.logicalArr[raw] = 0;
            out.physicalArr[raw] = 0;
            out.uniqueArr[raw] = 0;
        }
    }
    for (qint32 raw = n - 1; raw >= 1; --raw) {
        const qint32 p = graph.parents[raw];
        out.logicalArr[p] += out.logicalArr[raw];
        out.physicalArr[p] += out.physicalArr[raw];
        out.uniqueArr[p] += out.uniqueArr[raw];
    }

    // 3) Charge each family's shared base at its LCA and propagate upward.
    for (auto it = anchorCharge.constBegin(); it != anchorCharge.constEnd(); ++it) {
        qint32 node = it.key();
        const qint64 charge = it.value();
        while (node >= 0) {
            out.physicalArr[node] += charge;
            out.uniqueArr[node] += charge;
            node = graph.parents[node];
        }
    }

    out.summary.totalPhysical = out.physicalArr[0];
    out.summary.totalUnique = out.uniqueArr[0];
    out.summary.totalLogical = out.logicalArr[0];
    return out;
}

} // namespace ads::ReflinkAnalyzer
