#pragma once

#include "../model/FileGraph.h"

namespace ads {

/// Post-scan pass that turns per-file "freeable = physical" into honest
/// reflink-aware values on copy-on-write filesystems (btrfs, xfs, ...).
///
/// Method: for every regular file, read physical extents via FIEMAP. Files
/// whose physical extents overlap share those bytes (reflink clones). Within
/// a sharing group ("clone family"):
///   - the family's shared bytes are charged ONCE at the family's lowest
///     common ancestor (rollups above the LCA must not multi-count), and
///   - each member's individual freeable drops to the bytes it alone owns
///     (deleting one clone frees only its private extents).
///
/// Files on non-CoW filesystems keep freeable == physical (already exact).
/// This is an estimate in one case: when a family member lives outside the
/// scanned tree, the shared bytes can't be reclaimed by deleting inside the
/// tree — those nodes are flagged externalLinks, matching the hardlink rule.
namespace ReflinkAnalyzer {

    /// Returns a copy of `graph` with reflink-aware unique (freeable) sizes
    /// and shared-bytes corrections applied. On non-CoW roots, or when the
    /// kernel offers no FIEMAP, returns the graph unchanged.
    FileGraph analyze(const FileGraph &graph);

} // namespace ReflinkAnalyzer

} // namespace ads
