#pragma once

#include <QByteArray>
#include <QVector>
#include <QtTypes>

namespace ads {

/// One physical extent of a file: [physical, physical+length) on the device.
/// Two files sharing a physical extent are reflink (copy-on-write) clones of
/// each other for that range — deleting one frees none of the shared bytes.
struct Extent {
    quint64 physical = 0;
    quint64 length = 0;
    bool shared = false; // FIEMAP_EXTENT_SHARED
    bool encoded = false; // FIEMAP_EXTENT_ENCODED (compressed/encrypted)
};

namespace ExtentMap {

    /// Read a file's physical extents via FIEMAP. `fd` must be an open fd for
    /// the file (O_RDONLY, nofollow). Returns false when the filesystem does
    /// not support FIEMAP (caller treats the file as fully private).
    bool read(int fd, QVector<Extent> &out);

    /// True when the filesystem is known to do copy-on-write / reflinks
    /// (btrfs, xfs with reflink, bcachefs, ocfs2). On other filesystems the
    /// extent engine is skipped entirely — files are private by definition.
    bool filesystemSupportsReflink(const QString &fsType);

} // namespace ExtentMap

} // namespace ads
