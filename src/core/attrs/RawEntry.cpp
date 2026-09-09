#include "RawEntry.h"

#include <linux/fs.h>
#include <sys/stat.h>

namespace ads {

bool RawEntry::isDirectory() const
{
    return (mode & S_IFMT) == S_IFDIR;
}

bool RawEntry::isSymlink() const
{
    return (mode & S_IFMT) == S_IFLNK;
}

bool RawEntry::isRegularFile() const
{
    return (mode & S_IFMT) == S_IFREG;
}

bool RawEntry::isCompressed() const
{
    return fsFlags & FS_COMPR_FL;
}

} // namespace ads
