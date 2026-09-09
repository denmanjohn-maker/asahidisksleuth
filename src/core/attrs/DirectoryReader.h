#pragma once

#include "RawEntry.h"

#include <QVector>

#include <sys/types.h>

namespace ads {

/// Enumerates a directory's entries with full metadata, one batch per call.
///
/// Linux replacement for DiskSleuth's getattrlistbulk reader: getdents64 for
/// enumeration, statx (AT_SYMLINK_NOFOLLOW, relative to the dir fd) for
/// per-entry metadata. One syscall for the listing plus one per entry —
/// syscall-latency-bound, so the scan engine hides it with worker threads.
class DirectoryReader
{
public:
    /// Takes over reading directory fd `dirfd` (does not own it; caller
    /// manages lifetime via DirHandle).
    explicit DirectoryReader(int dirfd);
    ~DirectoryReader();

    DirectoryReader(const DirectoryReader &) = delete;
    DirectoryReader &operator=(const DirectoryReader &) = delete;

    /// Next batch of entries. Empty vector = end of directory.
    /// Sets `error` to the errno code on failure (returns empty too).
    QVector<RawEntry> nextBatch(int *error = nullptr);

private:
    int m_dirfd;
    int m_dupfd = -1; // dup'd fd for statx; getdents64 uses its own DIR*
    void *m_dir = nullptr; // DIR* from fdopendir on a second dup
};

} // namespace ads
