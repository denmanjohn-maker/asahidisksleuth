#include "DirectoryReader.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ads {

namespace {
    /// Batch size: matches the original's cadence of streaming entries to the
    /// graph builder per directory rather than buffering whole huge dirs.
    constexpr qsizetype kBatchSize = 512;
}

DirectoryReader::DirectoryReader(int dirfd)
    : m_dirfd(dirfd)
{
    // fdopendir consumes an fd, and we must not disturb the caller's handle;
    // use our own dup for enumeration, and a second dup kept open for statx
    // so paths resolve relative to the directory even after enumeration ends.
    const int enumFd = fcntl(dirfd, F_DUPFD_CLOEXEC, 3);
    if (enumFd >= 0)
        m_dir = fdopendir(enumFd);
    if (!m_dir && enumFd >= 0)
        close(enumFd);
    m_dupfd = fcntl(dirfd, F_DUPFD_CLOEXEC, 3);
}

DirectoryReader::~DirectoryReader()
{
    if (m_dir)
        closedir(static_cast<DIR *>(m_dir));
    if (m_dupfd >= 0)
        close(m_dupfd);
}

QVector<RawEntry> DirectoryReader::nextBatch(int *error)
{
    QVector<RawEntry> batch;
    if (error)
        *error = 0;
    if (!m_dir) {
        if (error)
            *error = EBADF;
        return batch;
    }
    batch.reserve(kBatchSize);

    while (batch.size() < kBatchSize) {
        errno = 0;
        dirent *ent = readdir(static_cast<DIR *>(m_dir));
        if (!ent) {
            if (errno != 0 && error)
                *error = errno;
            break;
        }
        if (ent->d_name[0] == '.'
            && (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;

        RawEntry entry;
        entry.name = QByteArray(ent->d_name);

        if (m_dupfd >= 0) {
            struct statx stx {};
            if (statx(m_dupfd, ent->d_name, AT_SYMLINK_NOFOLLOW,
                      STATX_TYPE | STATX_SIZE | STATX_BLOCKS | STATX_INO | STATX_NLINK | STATX_MNT_ID,
                      &stx)
                == 0) {
                entry.mode = stx.stx_mode;
                entry.fileID = stx.stx_ino;
                entry.linkCount = stx.stx_nlink;
                entry.mntID = stx.stx_mnt_id;
                entry.device = (quint64(stx.stx_dev_major) << 32) | stx.stx_dev_minor;
                if ((stx.stx_mask & STATX_SIZE) && (stx.stx_mode & S_IFMT) != S_IFDIR)
                    entry.logical = qint64(stx.stx_size);
                if (stx.stx_mask & STATX_BLOCKS)
                    entry.physical = qint64(stx.stx_blocks) * 512;
            } else {
                // Entry vanished between readdir and statx — skip it.
                continue;
            }
        }

        batch.append(std::move(entry));
    }
    return batch;
}

} // namespace ads
