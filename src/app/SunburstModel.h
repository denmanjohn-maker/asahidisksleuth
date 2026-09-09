#pragma once

#include "ScanController.h"
#include "SunburstLayout.h"

#include <QAbstractListModel>

namespace ads {

/// Exposes the sunburst segments for the current (graph, focus, lens) to QML.
/// Recomputes whenever any of the three change.
class SunburstModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        NodeRole = Qt::UserRole + 1,
        RingRole,
        StartRole,
        EndRole,
        HueRole,
        IsDirRole,
        BytesRole,
        NameRole,
        IsOtherRole,
    };

    explicit SunburstModel(QObject *parent = nullptr);

    void setController(ScanController *controller);

    /// Segment lookup for hit-testing from QML (row or -1).
    Q_INVOKABLE int segmentAt(double fraction, int ring) const;

    /// Role constants exposed to QML so views don't hardcode numbers.
    Q_INVOKABLE QVariantMap roles() const
    {
        return {{QStringLiteral("node"), NodeRole},
                {QStringLiteral("ring"), RingRole},
                {QStringLiteral("startFraction"), StartRole},
                {QStringLiteral("endFraction"), EndRole},
                {QStringLiteral("hue"), HueRole},
                {QStringLiteral("isDir"), IsDirRole},
                {QStringLiteral("bytes"), BytesRole},
                {QStringLiteral("segName"), NameRole},
                {QStringLiteral("isOther"), IsOtherRole}};
    }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void rebuild();

    ScanController *m_controller = nullptr;
    QVector<SunburstSegment> m_segments;
};

} // namespace ads
