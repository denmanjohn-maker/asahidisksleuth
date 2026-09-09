#include "ExtentMap.h"

#include <errno.h>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <sys/ioctl.h>

#include <cstring>
#include <vector>

namespace ads::ExtentMap {

bool filesystemSupportsReflink(const QString &fsType)
{
    return fsType == u"btrfs" || fsType == u"xfs" || fsType == u"bcachefs" || fsType == u"ocfs2";
}

bool read(int fd, QVector<Extent> &out)
{
    // Two-pass: first ask how many extents, then fetch. Files with extreme
    // fragmentation (> ~64k extents) are rare; cap and mark truncation by
    // simply reading what fits — shared accounting stays conservative.
    constexpr quint32 kMaxExtents = 65536;

    struct fiemap probe {};
    probe.fm_start = 0;
    probe.fm_length = ~0ULL;
    probe.fm_extent_count = 0;
    if (::ioctl(fd, FS_IOC_FIEMAP, &probe) != 0)
        return false; // EOPNOTSUPP and friends: treat as private

    const quint32 count = std::min(probe.fm_mapped_extents, kMaxExtents);
    if (count == 0)
        return true; // empty file, no extents — supported, nothing shared

    std::vector<unsigned char> buffer(sizeof(fiemap) + count * sizeof(fiemap_extent));
    auto *map = reinterpret_cast<fiemap *>(buffer.data());
    std::memset(map, 0, buffer.size());
    map->fm_start = 0;
    map->fm_length = ~0ULL;
    map->fm_extent_count = count;
    if (::ioctl(fd, FS_IOC_FIEMAP, map) != 0)
        return false;

    out.reserve(out.size() + map->fm_mapped_extents);
    for (quint32 i = 0; i < map->fm_mapped_extents; ++i) {
        const fiemap_extent &e = map->fm_extents[i];
        Extent ext;
        ext.physical = e.fe_physical;
        ext.length = e.fe_length;
        ext.shared = (e.fe_flags & FIEMAP_EXTENT_SHARED) != 0;
        ext.encoded = (e.fe_flags & FIEMAP_EXTENT_ENCODED) != 0;
        out.append(ext);
    }
    return true;
}

} // namespace ads::ExtentMap
