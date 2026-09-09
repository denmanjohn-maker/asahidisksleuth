# The Linux Truth Engine

How asahidisksleuth measures disk usage honestly on Linux, and where the limits are.
This is the Linux counterpart to the original macOS `truth-engine.md`.

## Two lenses, both from the kernel

Every file and folder reports:

| Lens | Source | Meaning |
|---|---|---|
| **Logical** | `statx.stx_size` | What the file claims to be (`ls -l`, `du --apparent-size`) |
| **Physical** | `statx.stx_blocks × 512` | What the disk actually holds (`du`) |

Directory sizes are the sum of their subtree, computed bottom-up in the graph.

## What we account for exactly

**Hardlinks** share one inode's content across several paths. Rollups would count
those bytes once per link, so we charge them **once** at the lowest common ancestor
(LCA) of the link group: the file's physical bytes appear in every ancestor the whole
group shares, but the duplicated copies are subtracted above the LCA. Deleting one
link frees nothing (other links keep the content alive) — per-file "freeable" is 0
for a multiply-linked file, and the group's bytes become freeable only when every
link is inside the scanned tree.

**Sparse files** have holes that are never allocated: `stx_blocks × 512 < stx_size`.
Physical < logical is real, and we flag the node sparse.

**btrfs transparent compression** (`compress=zstd`, the Asahi default) makes physical
smaller than logical. That's genuine savings, reported as-is; compressed files are
flagged via `FS_IOC_GETFLAGS` / `FS_COMPR_FL`.

**Cross-mount boundaries** are not traversed by default: mount points onto other
filesystems are flagged "other mount" and their content excluded, so scanning `/`
doesn't wander into `/proc`, `/sys`, or other volumes. `--cross-file-systems` opts in.

**Cycles** (symlink loops, bind-mount recursion) are killed by a `(device, inode)`
visited set; a directory seen twice is flagged "already counted".

## Honest denials

A directory we can't read (`EACCES`/`EPERM`) is recorded in a permission ledger —
never silently skipped. The node is flagged ⛔, the count appears in the summary
("N folders could not be read — totals undercount"), and the first 2,000 paths are
kept for inspection. Running with elevated privileges closes the gap.

## btrfs shared pools

Asahi Fedora puts `/` and `/home` (and any other subvolumes) on **one btrfs pool**.
`statvfs` therefore reports the same total/free at every subvolume — there is no
per-subvolume "used" without quotas (qgroups). The overview shows the pool-wide
numbers plus an explicit note, rather than pretending subvolumes have independent
usage. Snapshots are listed via `btrfs subvolume list -s`.

## What's *not* available on Linux (yet)

**Per-file "freeable"** — the exact bytes a single `unlink` returns — has no cheap
syscall on Linux. APFS exposes it directly (`ATTR_CMNEXT_PRIVATESIZE`); btrfs/xfs
reflink clones share extents invisibly. Today, per-file freeable equals physical
(exact for unshared files, and correct for hardlinks via the LCA rule above).

Phase 2 of the engine adds a btrfs-aware approximation: enumerate file extents with
`FIEMAP`, detect physical-extent sharing between files (the jdupes approach), and
feed clone families into the same LCA accounting the hardlink path already uses.
Until then, the UI shows Logical/Physical and never invents a freeable number.
