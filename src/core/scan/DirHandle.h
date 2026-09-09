#pragma once

#include <QAtomicInt>

#include <unistd.h>

namespace ads {

/// Refcounted, heap-allocated owner of an open directory fd. A directory's
/// handle stays alive while any child work item still needs it for openat(2),
/// then closes the fd and frees itself exactly once — keeping the number of
/// open fds bounded without path re-resolution.
///
/// Heap lifetime is essential: work items referencing this handle are
/// processed by other threads long after the enumerating worker's stack
/// frame is gone. Last release() deletes the object.
class DirHandle
{
public:
    static DirHandle *create(int fd, int initialRefs = 1)
    {
        return new DirHandle(fd, initialRefs);
    }

    void retain() { m_refs.ref(); }

    void release()
    {
        if (!m_refs.deref()) {
            ::close(fd);
            delete this;
        }
    }

    const int fd;

private:
    explicit DirHandle(int fd, int initialRefs)
        : fd(fd)
        , m_refs(initialRefs)
    {
    }
    ~DirHandle() = default;

    QAtomicInt m_refs;
};

} // namespace ads
