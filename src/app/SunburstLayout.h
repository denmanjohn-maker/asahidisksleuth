#pragma once

#include "../core/model/FileGraph.h"

#include <QString>
#include <QVector>

namespace ads {

/// One annular sector of the sunburst. Pure geometry — no drawing.
struct SunburstSegment {
    qint32 node = -1; // graph node, or -1 for an "other" aggregate
    qint32 otherParent = -1; // set when node == -1
    int otherCount = 0;
    int ring = 0;
    double startFraction = 0; // 0..1 from 12 o'clock, clockwise
    double endFraction = 0;
    /// Hue inherited from the ring-0 ancestor for stable family coloring.
    double hue = 0;
    bool isDirectory = false;
    quint16 flags = 0;
    qint64 bytes = 0;
    QString name;

    double midFraction() const { return (startFraction + endFraction) / 2; }
    double span() const { return endFraction - startFraction; }
};

namespace SunburstLayout {

    /// Lay out `rings` rings of descendants of `focus`, each segment's angular
    /// span proportional to its share of the focus subtree in the active lens.
    /// Children below `minFraction` of the circle collapse into an "other" arc.
    QVector<SunburstSegment> segments(const FileGraph &graph, NodeID focus, SizeLens lens,
                                      int rings = 4, double minFraction = 0.004);

    /// Segment under a point in polar coordinates, or nullptr.
    const SunburstSegment *hitTest(const QVector<SunburstSegment> &segments, double fraction,
                                   int ring);

} // namespace SunburstLayout

} // namespace ads
