#pragma once

#include "GraphBuilder.h"
#include "../model/FileGraph.h"

#include <QAtomicInt>
#include <QString>

#include <memory>

namespace ads {

struct ScanOptions {
    /// Max directories being read concurrently.
    /// Scanning is syscall-latency-bound, not CPU-bound: workers spend most of
    /// their time waiting on metadata reads, so the sweet spot is far more
    /// workers than cores (the original measured 64 ≈ 1.8× faster than
    /// 2×cores; flat beyond).
    int maxConcurrency = 32;
    /// Traverse onto other mounts at mount points.
    bool crossVolumes = false;
    /// After the scan, run the FIEMAP reflink pass (CoW filesystems only) so
    /// the Unique/freeable lens accounts for clone families. Off by default:
    /// it costs one open+ioctl per regular file.
    bool detectReflinks = false;
};

struct ScanResult {
    FileGraph graph;
    QString error; // empty on success
};

/// Concurrent filesystem traversal. The scan runs on a private thread pool;
/// callers block on `wait()` or poll `progress()`; `cancel()` still yields a
/// finalized (partial) graph.
class ScanSession
{
public:
    ~ScanSession();
    ScanSession(ScanSession &&) = delete;

    void cancel();
    /// Blocks until the graph is finalized (partial if cancelled).
    ScanResult wait();
    ScanProgress progress() const;
    bool isFinished() const;

    // Defined in ScanEngine.cpp; exposed (not private) so the worker threads
    // in the same translation unit can drive the traversal internals.
    struct Private;

private:
    friend class ScanEngine;
    explicit ScanSession(std::unique_ptr<Private> d);
    std::unique_ptr<Private> d;
};

class ScanEngine
{
public:
    /// Validate the root and start the traversal. Returns nullptr with
    /// `error` set on immediate failure (not a directory / cannot open).
    static std::unique_ptr<ScanSession> scan(const QString &path,
                                             const ScanOptions &options = {},
                                             QString *error = nullptr);
};

} // namespace ads
