#pragma once

#include <QString>
#include <QVector>
#include <QtTypes>

namespace ads {

/// Identity and capacity of the mount containing a path.
struct VolumeInfo {
    QString mountPoint; // e.g. "/home"
    QString fsType; // e.g. "btrfs"
    QString device; // e.g. "/dev/nvme0n1p6"
    bool readOnly = false;
    qint64 totalBytes = 0;
    qint64 freeBytes = 0; // f_bfree — including root-reserved
    qint64 availableBytes = 0; // f_bavail — what unprivileged users get
    qint64 usedBytes = 0; // total - free
    bool isBtrfs = false;
};

/// A btrfs snapshot (read-only subvolume) under a mount.
struct BtrfsSnapshot {
    QString path; // relative to the mount, e.g. "@snapshots/2026-09-01"
    qint64 id = 0;
    bool readOnly = true;
};

namespace VolumeService {

    /// Identity + capacity for the mount containing `path`.
    /// Returns false on failure (path missing, unreadable mountinfo).
    bool identity(const QString &path, VolumeInfo *out, QString *error = nullptr);

    /// Read-only btrfs snapshots under the mount containing `path`.
    /// Empty (not an error) on non-btrfs volumes or when `btrfs` is missing.
    QVector<BtrfsSnapshot> btrfsSnapshots(const QString &path);

} // namespace VolumeService

} // namespace ads
