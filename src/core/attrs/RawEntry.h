#pragma once

#include <QByteArray>
#include <QtTypes>

namespace ads {

/// One parsed directory entry, populated from statx(2).
/// Sizes are -1 when not applicable (e.g. directories) or unavailable.
struct RawEntry {
    /// Raw bytes; Linux filenames are not guaranteed UTF-8.
    QByteArray name;
    /// d_type-style mode bits from statx (S_IFMT masked value, e.g. S_IFDIR).
    quint32 mode = 0;
    quint64 fileID = 0; // stx_ino
    quint32 linkCount = 1; // stx_nlink
    /// stx_size; -1 for directories.
    qint64 logical = -1;
    /// stx_blocks * 512; -1 when blocks were not returned.
    qint64 physical = -1;
    /// FS_IOC_GETFLAGS attributes (0 when unavailable); FS_COMPR_FL etc.
    quint32 fsFlags = 0;
    /// stx_mnt_id — stable mount identifier for cross-volume checks.
    quint64 mntID = 0;
    /// stx_dev — for hardlink identity and the visited set.
    quint64 device = 0;

    bool isDirectory() const;
    bool isSymlink() const;
    bool isRegularFile() const;
    /// physical < logical means holes or transparent compression.
    bool isSparse() const { return logical > 0 && physical >= 0 && physical < logical; }
    bool isCompressed() const;
};

} // namespace ads
