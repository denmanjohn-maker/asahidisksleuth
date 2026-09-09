#pragma once

#include "DirHandle.h"

#include <QMutex>
#include <QString>
#include <QVector>
#include <QWaitCondition>

namespace ads {

/// One unit of traversal work: a directory awaiting enumeration.
struct DirWork {
    qint32 nodeID = -1;
    /// Handle of the parent directory (openat base). nullptr for the root.
    /// The work item holds one reference; released when the item is processed
    /// or dropped.
    DirHandle *parent = nullptr;
    /// Entry name within parent (raw bytes); full path for the root item.
    QByteArray name;
    /// Absolute path (for the permission ledger and progress display).
    QString path;
};

/// Distributes directory work across scanner threads. LIFO order keeps the
/// traversal frontier narrow (bounding open parent fds), and termination is
/// detected when no work is pending and none is in flight.
///
/// Direct port of the Swift actor onto QMutex + QWaitCondition.
class WorkQueue
{
public:
    /// Takes over the parent-fd references held by `items`.
    void push(const QVector<DirWork> &items)
    {
        QMutexLocker lock(&m_mutex);
        if (m_finished) {
            for (const DirWork &item : items)
                if (item.parent)
                    item.parent->release();
            return;
        }
        m_stack.append(items);
        m_waiters.wakeAll();
    }

    /// Next work item; blocks while work may still arrive. Returns false when
    /// the traversal is complete (or cancelled).
    bool next(DirWork &out)
    {
        QMutexLocker lock(&m_mutex);
        for (;;) {
            if (m_finished)
                return false;
            if (!m_stack.isEmpty()) {
                out = m_stack.takeLast();
                ++m_inFlight;
                return true;
            }
            if (m_inFlight == 0) {
                finishAllLocked();
                return false;
            }
            m_waiters.wait(&m_mutex);
        }
    }

    /// Must be called exactly once per item obtained from next().
    void complete()
    {
        QMutexLocker lock(&m_mutex);
        --m_inFlight;
        if (m_inFlight == 0 && m_stack.isEmpty())
            finishAllLocked();
    }

    /// Abort: wake every waiter and drop pending work (releasing parent fds).
    void cancelAll()
    {
        QMutexLocker lock(&m_mutex);
        finishAllLocked();
    }

private:
    void finishAllLocked()
    {
        m_finished = true;
        for (DirWork &item : m_stack)
            if (item.parent)
                item.parent->release();
        m_stack.clear();
        m_waiters.wakeAll();
    }

    QVector<DirWork> m_stack; // LIFO
    int m_inFlight = 0;
    bool m_finished = false;
    QMutex m_mutex;
    QWaitCondition m_waiters;
};

} // namespace ads
