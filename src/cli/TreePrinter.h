#pragma once

#include "Style.h"

#include "../core/model/FileGraph.h"

class QTextStream;

namespace ads::cli {

/// Renders a FileGraph as an indented tree with honest sizes.
class TreePrinter
{
public:
    TreePrinter(const FileGraph &graph, SizeLens lens, const Style &style, bool binaryUnits,
                int maxDepth, int topPerLevel)
        : m_graph(graph)
        , m_lens(lens)
        , m_style(style)
        , m_binaryUnits(binaryUnits)
        , m_maxDepth(maxDepth)
        , m_topPerLevel(topPerLevel)
    {
    }

    /// Prints to `out` (which the caller flushes first if it holds prior output).
    void printTree(QTextStream &out) const;

    static QString badges(NodeFlags flags, const Style &style);

private:
    QString fmt(qint64 bytes) const;
    QString bar(qint64 size, qint64 rootSize) const;
    void printNode(QTextStream &out, NodeID node, int depth, qint64 rootSize) const;

    const FileGraph &m_graph;
    SizeLens m_lens;
    const Style &m_style;
    bool m_binaryUnits;
    int m_maxDepth;
    int m_topPerLevel;
};

} // namespace ads::cli
