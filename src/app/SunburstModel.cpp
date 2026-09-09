#include "SunburstModel.h"

namespace ads {

SunburstModel::SunburstModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void SunburstModel::setController(ScanController *controller)
{
    if (m_controller == controller)
        return;
    if (m_controller)
        m_controller->disconnect(this);
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &ScanController::graphChanged, this, &SunburstModel::rebuild);
        connect(m_controller, &ScanController::lensChanged, this, &SunburstModel::rebuild);
        connect(m_controller, &ScanController::focusChanged, this, &SunburstModel::rebuild);
    }
    rebuild();
}

int SunburstModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_segments.size());
}

QVariant SunburstModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_segments.size())
        return {};
    const SunburstSegment &s = m_segments[index.row()];
    switch (role) {
    case NodeRole:
        return s.node;
    case RingRole:
        return s.ring;
    case StartRole:
        return s.startFraction;
    case EndRole:
        return s.endFraction;
    case HueRole:
        return s.hue;
    case IsDirRole:
        return s.isDirectory;
    case BytesRole:
        return s.bytes;
    case NameRole:
        return s.name;
    case IsOtherRole:
        return s.node < 0;
    }
    return {};
}

QHash<int, QByteArray> SunburstModel::roleNames() const
{
    return {
        {NodeRole, "node"},     {RingRole, "ring"},   {StartRole, "startFraction"},
        {EndRole, "endFraction"}, {HueRole, "hue"},   {IsDirRole, "isDir"},
        {BytesRole, "bytes"},   {NameRole, "segName"}, {IsOtherRole, "isOther"},
    };
}

int SunburstModel::segmentAt(double fraction, int ring) const
{
    for (int i = 0; i < m_segments.size(); ++i)
        if (m_segments[i].ring == ring && fraction >= m_segments[i].startFraction
            && fraction < m_segments[i].endFraction)
            return i;
    return -1;
}

void SunburstModel::rebuild()
{
    beginResetModel();
    m_segments.clear();
    if (m_controller && m_controller->hasGraph()) {
        const FileGraph *g = m_controller->graph();
        m_segments = SunburstLayout::segments(*g, NodeID{m_controller->focusNode()},
                                              SizeLens(m_controller->lens()));
    }
    endResetModel();
}

} // namespace ads
