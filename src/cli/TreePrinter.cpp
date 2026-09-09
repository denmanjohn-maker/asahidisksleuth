#include "TreePrinter.h"

#include "../core/util/ByteFormat.h"

#include <QTextStream>

#include <algorithm>
#include <cstdio>

namespace ads::cli {

QString TreePrinter::fmt(qint64 bytes) const
{
    return formatBytes(bytes, m_binaryUnits);
}

QString TreePrinter::badges(NodeFlags flags, const Style &style)
{
    QStringList parts;
    if (flags.cloned())
        parts.append(QStringLiteral("⧉"));
    if (flags.sparse())
        parts.append(QStringLiteral("▤"));
    if (flags.hardlinked())
        parts.append(QStringLiteral("⛓"));
    if (flags.compressed())
        parts.append(QStringLiteral("▣"));
    if (flags.accessDenied())
        parts.append(QStringLiteral("⛔"));
    if (flags.otherVolume())
        parts.append(QStringLiteral("⇥ other mount"));
    if (flags.duplicate())
        parts.append(QStringLiteral("↩ already counted"));
    if (flags.externalLinks())
        parts.append(QStringLiteral("✳ shared outside"));
    return parts.isEmpty() ? QString() : QStringLiteral("  ") + style.dim(parts.join(u' '));
}

void TreePrinter::printTree(QTextStream &out) const
{
    printNode(out, m_graph.root(), 0, std::max(m_graph.size(m_graph.root(), m_lens), qint64(1)));
}

QString TreePrinter::bar(qint64 size, qint64 rootSize) const
{
    constexpr int width = 10;
    const double fraction = double(size) / double(rootSize);
    const int filled = int(fraction * width + 0.5);
    const QString blocks = QString(qMax(filled, 0), u'█') + QString(qMax(width - filled, 0), u'·');
    return m_style.dim(QStringLiteral("▕")) + m_style.cyan(blocks) + m_style.dim(QStringLiteral("▏"));
}

void TreePrinter::printNode(QTextStream &out, NodeID node, int depth, qint64 rootSize) const
{
    const qint64 size = m_graph.size(node, m_lens);
    const NodeFlags flags = m_graph.flags(node);
    const bool isDir = flags.kind() == NodeKind::Directory;
    QString displayName = depth == 0 ? m_graph.rootPath : m_graph.name(node);
    const QString sizeText = fmt(size).leftJustified(11, u' ');

    const QString indent(2 * depth, u' ');
    const QString name = isDir ? m_style.boldBlue(displayName.endsWith(u'/') ? displayName : displayName + u'/')
                               : displayName;
    out << bar(size, rootSize) << u' ' << m_style.bold(sizeText) << indent << name
        << badges(flags, m_style) << '\n';

    if (!isDir || depth >= m_maxDepth)
        return;
    const QVector<NodeID> children = m_graph.children(node);
    // Children come sorted by physical; re-rank by the active lens.
    QVector<NodeID> ranked = children;
    if (m_lens != SizeLens::Physical)
        std::sort(ranked.begin(), ranked.end(),
                  [&](NodeID a, NodeID b) { return m_graph.size(a, m_lens) > m_graph.size(b, m_lens); });

    int shown = 0;
    int restCount = 0;
    qint64 restBytes = 0;
    for (const NodeID child : ranked) {
        if (shown < m_topPerLevel) {
            printNode(out, child, depth + 1, rootSize);
            ++shown;
        } else {
            ++restCount;
            restBytes += m_graph.size(child, m_lens);
        }
    }
    if (restCount > 0) {
        const QString childIndent(2 * (depth + 1), u' ');
        const QString restText = fmt(restBytes).leftJustified(11, u' ');
        out << bar(restBytes, rootSize) << u' ' << m_style.dim(restText) << childIndent
            << m_style.dim(QStringLiteral("… %1 more").arg(restCount)) << '\n';
    }
}

} // namespace ads::cli
