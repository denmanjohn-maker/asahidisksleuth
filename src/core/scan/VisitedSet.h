#pragma once

#include <QMutex>
#include <QSet>
#include <QtTypes>

namespace ads {

/// Thread-safe set of directories already traversed, keyed by (device, inode).
/// Kills mount loops and directory-hardlink weirdness.
class VisitedSet
{
public:
    /// Returns true if this directory had not been seen before (caller may
    /// traverse).
    bool markVisited(quint64 device, quint64 inode)
    {
        QMutexLocker lock(&m_mutex);
        const QPair<quint64, quint64> key(device, inode);
        if (m_seen.contains(key))
            return false;
        m_seen.insert(key);
        return true;
    }

private:
    QSet<QPair<quint64, quint64>> m_seen;
    QMutex m_mutex;
};

} // namespace ads
