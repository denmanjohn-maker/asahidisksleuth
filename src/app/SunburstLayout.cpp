#include "SunburstLayout.h"

#include <algorithm>
#include <functional>

namespace ads::SunburstLayout {

QVector<SunburstSegment> segments(const FileGraph &graph, NodeID focus, SizeLens lens, int rings,
                                  double minFraction)
{
    const qint64 focusSize = graph.size(focus, lens);
    if (focusSize <= 0)
        return {};
    QVector<SunburstSegment> result;

    // Recursive layout; std::function for the lambda self-call.
    std::function<void(NodeID, int, double, double, double, bool)> layout =
        [&](NodeID parent, int ring, double start, double end, double hue, bool hasHue) {
            if (ring >= rings || end - start <= 0.0005)
                return;
            const QVector<NodeID> children = graph.children(parent);
            if (children.isEmpty())
                return;

            QVector<NodeID> ranked = children;
            if (lens != SizeLens::Physical)
                std::sort(ranked.begin(), ranked.end(), [&](NodeID a, NodeID b) {
                    return graph.size(a, lens) > graph.size(b, lens);
                });

            double cursor = start;
            qint64 otherBytes = 0;
            int otherCount = 0;
            for (int index = 0; index < ranked.size(); ++index) {
                const NodeID child = ranked[index];
                const qint64 bytes = std::max(graph.size(child, lens), qint64(0));
                const double fraction = double(bytes) / double(focusSize);
                const double span = fraction;
                if (span < minFraction || cursor + span > end + 0.000001) {
                    otherBytes += bytes;
                    ++otherCount;
                    continue;
                }
                const double segmentHue = hasHue ? hue : double((index * 5) % 13) / 13.0;
                const NodeFlags flags = graph.flags(child);
                SunburstSegment seg;
                seg.node = child.raw;
                seg.ring = ring;
                seg.startFraction = cursor;
                seg.endFraction = cursor + span;
                seg.hue = segmentHue;
                seg.isDirectory = flags.kind() == NodeKind::Directory;
                seg.flags = flags.rawValue();
                seg.bytes = bytes;
                seg.name = graph.name(child);
                result.append(seg);
                if (flags.kind() == NodeKind::Directory)
                    layout(child, ring + 1, cursor, cursor + span, segmentHue, true);
                cursor += span;
            }
            if (otherCount > 0 && otherBytes > 0) {
                const double span =
                    std::min(double(otherBytes) / double(focusSize), std::max(end - cursor, 0.0));
                if (span > 0.0005) {
                    SunburstSegment seg;
                    seg.node = -1;
                    seg.otherParent = parent.raw;
                    seg.otherCount = otherCount;
                    seg.ring = ring;
                    seg.startFraction = cursor;
                    seg.endFraction = cursor + span;
                    seg.hue = hasHue ? hue : 0;
                    seg.isDirectory = false;
                    seg.flags = quint16(NodeKind::Other);
                    seg.bytes = otherBytes;
                    seg.name = QStringLiteral("%1 smaller items").arg(otherCount);
                    result.append(seg);
                }
            }
        };

    layout(focus, 0, 0.0, 1.0, 0.0, false);
    return result;
}

const SunburstSegment *hitTest(const QVector<SunburstSegment> &segments, double fraction, int ring)
{
    for (const SunburstSegment &s : segments)
        if (s.ring == ring && fraction >= s.startFraction && fraction < s.endFraction)
            return &s;
    return nullptr;
}

} // namespace ads::SunburstLayout
