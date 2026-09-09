#pragma once

#include "../core/scan/ScanEngine.h"
#include "../core/volume/VolumeInfo.h"

#include <QAbstractListModel>
#include <QObject>
#include <QTimer>

#include <memory>

namespace ads {

/// QML-facing controller: owns the current scan session and the resulting
/// graph, exposes navigation state (focus/selection/breadcrumbs), the active
/// lens, and live progress. Runs the engine's polling on a QTimer.
class ScanController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY graphChanged)
    Q_PROPERTY(qint64 filesSeen READ filesSeen NOTIFY progressChanged)
    Q_PROPERTY(qint64 physicalBytes READ physicalBytes NOTIFY progressChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY progressChanged)
    Q_PROPERTY(int lens READ lens WRITE setLens NOTIFY lensChanged)
    Q_PROPERTY(qint32 focusNode READ focusNode WRITE setFocusNode NOTIFY focusChanged)
    Q_PROPERTY(qint32 selectedNode READ selectedNode WRITE setSelectedNode NOTIFY selectionChanged)
    Q_PROPERTY(QStringList breadcrumbs READ breadcrumbs NOTIFY focusChanged)
    Q_PROPERTY(bool hasGraph READ hasGraph NOTIFY graphChanged)

public:
    enum State { Welcome, Scanning, Results };
    Q_ENUM(State)

    explicit ScanController(QObject *parent = nullptr);
    ~ScanController() override;

    State state() const { return m_state; }
    QString rootPath() const;
    bool hasGraph() const { return m_graph.has_value(); }

    qint64 filesSeen() const { return m_progress.filesSeen; }
    qint64 physicalBytes() const { return m_progress.physicalBytes; }
    QString currentPath() const { return m_progress.currentPath; }

    int lens() const { return int(m_lens); }
    void setLens(int lens);

    qint32 focusNode() const { return m_focus.raw; }
    void setFocusNode(qint32 raw);

    qint32 selectedNode() const { return m_selected.raw; }
    void setSelectedNode(qint32 raw);

    QStringList breadcrumbs() const;

    Q_INVOKABLE void scan(const QString &path);
    Q_INVOKABLE void pickAndScan(); // folder picker (portal)
    Q_INVOKABLE void cancelScan();
    Q_INVOKABLE void reset();

    // Graph accessors for QML (IDs are stable within one graph).
    Q_INVOKABLE QString nodeName(qint32 raw) const;
    Q_INVOKABLE QString nodePath(qint32 raw) const;
    Q_INVOKABLE qint64 nodeSize(qint32 raw) const; // in active lens
    Q_INVOKABLE qint64 nodeLogical(qint32 raw) const;
    Q_INVOKABLE qint64 nodePhysical(qint32 raw) const;
    Q_INVOKABLE qint64 nodeUnique(qint32 raw) const;
    Q_INVOKABLE bool nodeIsDir(qint32 raw) const;
    Q_INVOKABLE QString nodeBadges(qint32 raw) const;
    Q_INVOKABLE QVector<qint32> nodeChildren(qint32 raw) const; // lens-sorted
    Q_INVOKABLE qint32 nodeParent(qint32 raw) const;

    Q_INVOKABLE QString formatBytes(qint64 bytes) const;

    /// Summary numbers for the results header.
    Q_INVOKABLE qint64 totalFiles() const;
    Q_INVOKABLE qint64 totalDirs() const;
    Q_INVOKABLE qint64 deniedCount() const;
    Q_INVOKABLE double wallSeconds() const;

    /// Actions.
    Q_INVOKABLE void revealInFileManager(qint32 raw) const;
    Q_INVOKABLE bool moveToTrash(qint32 raw); // patches graph in memory

    const FileGraph *graph() const { return m_graph ? &*m_graph : nullptr; }

signals:
    void stateChanged();
    void progressChanged();
    void graphChanged();
    void lensChanged();
    void focusChanged();
    void selectionChanged();
    void errorOccurred(const QString &message);

private:
    void pollProgress();
    void finishScan();

    std::unique_ptr<ScanSession> m_session;
    std::optional<FileGraph> m_graph;
    QTimer m_pollTimer;
    ScanProgress m_progress;
    State m_state = Welcome;
    SizeLens m_lens = SizeLens::Physical;
    NodeID m_focus{0};
    NodeID m_selected{0};
};

} // namespace ads
