# AsahiDiskSleuth — Port Plan

**Goal:** Port DiskSleuth (macOS/APFS disk analyzer, Swift + SwiftUI) to a slick native
KDE Plasma application on Fedora Asahi Remix (aarch64, btrfs).

**Target stack:** C++23 · Qt 6.11 · Kirigami 6 (KF6) · CMake · ECM
**Reference codebase:** `/home/jdenman/macdisksleuth` (~4,800 LoC Swift)
**Working directory:** `/home/jdenman/asahidisksleuth`

**Decisions locked in:**
- Stack: C++ with Qt6/Kirigami
- Freeable lens: Phase 1 = Logical + Physical only; Phase 2 = btrfs FIEMAP-based approximation
- Scope: CLI first, then GUI on the same engine
- Snapshots: basic btrfs subvolume/snapshot listing

---

## 1. Environment & constraints (verified)

- Fedora Linux Asahi Remix 44, KDE Plasma, kernel `7.1.13-401.asahi.fc44.aarch64+16k`
- `/` and `/home` are **btrfs** on `/dev/nvme0n1p6` (single pool — subvolumes share space)
- 8 cores, 15 GiB RAM, 187 GiB free on `/home`
- Toolchain present: `g++`, `cmake`, `rpm`, `dnf`. Qt6/KF6 runtime present.
- Available via dnf (verified): `qt6-qtbase-devel 6.11.2`, `qt6-qtdeclarative-devel 6.11.2`,
  `kf6-kirigami-devel 6.29.0`, `extra-cmake-modules`, `ninja-build`, `clang` (optional)
- No swift/rust toolchains installed — consistent with the C++/Qt decision.

### Bootstrap dependencies (first action of Phase 0)

```
sudo dnf install -y \
  gcc-c++ cmake ninja-build extra-cmake-modules \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel \
  kf6-kirigami-devel kf6-kcoreaddons-devel kf6-ki18n-devel \
  kf6-kconfig-devel kf6-kcrash-devel \
  qt6-qtbase-doc kf6-kirigami-doc        # optional, docs
```

---

## 2. Architecture overview

Mirror the original's clean separation: **engine library → CLI → GUI**, one repo.

```
asahidisksleuth/
├── CMakeLists.txt                  # top-level: C++23, Qt6, KF6, ECM, options
├── cmake/                          # ECM helpers, install config
├── src/
│   ├── core/                       # libasahidisksleuth (STATIC, no Qt GUI deps)
│   │   ├── model/                  # FileGraph, NodeFlags, Sizes, ScanSummary
│   │   ├── scan/                   # ScanEngine, WorkQueue, GraphBuilder, DirHandle
│   │   ├── attrs/                  # DirectoryReader (getdents64+statx), RawEntry
│   │   ├── volume/                 # VolumeInfo (statvfs+mountinfo), BtrfsSnapshots
│   │   └── util/                   # ByteFormat, Subprocess, Trash (FreeDesktop)
│   ├── cli/                        # asahidisksleuth — scan/top/info/overview/snapshots
│   └── app/                        # asahidisksleuth-app — Kirigami GUI
│       ├── qml/                    # Main.qml, SunburstView.qml, ResultsView.qml, ...
│       ├── model/                  # QML-facing bridges: ScanController, TreeModel, ...
│       └── resources/              # icons, desktop file, appstream metainfo
├── tests/                          # Catch2 unit + fixture tests (real fs fixtures)
├── packaging/                      # RPM .spec (later), flatpak manifest (optional, later)
├── docs/                           # truth-engine-linux.md, architecture notes
└── README.md
```

**Build:** CMake + ECM + Ninja. Two executables + one static core lib + tests.
Qt Core only in `core/` (QString, QByteArray, QThreadPool); Qt Quick/Kirigami only in `app/`.

---

## 3. Porting map (macOS → Linux)

### 3.1 Carried over conceptually (rewritten in C++, logic preserved)

| Swift component | LoC | C++ fate |
|---|---|---|
| `FileGraph` (struct-of-arrays, CSR children) | 165 | **Direct port.** `QVector<int32_t>` etc. + name `QByteArray` arena. Same ~46 B/node layout, tombstone delete, `removing()` O(depth) patch. |
| `GraphBuilder` (hardlink registry, LCA rollup, bottom-up sort) | 367 | **Direct port.** The intellectual core — pure integer math. `std::unordered_map` for registries. |
| `WorkQueue`, `ScanEngine` (LIFO queue, fd-bounded workers, progress stream, cancel) | ~500 | **Port to QtConcurrent/QThreadPool + QMutex/QWaitCondition.** Same 100 ms progress signal, same cancellation → partial graph. |
| `SunburstLayout` (pure polar geometry) | 113 | **Direct port** — pure math, feeds QML. |
| `ByteCount` formatting, tree printer, JSON output | ~150 | Trivial ports. |
| CLI command structure (scan/top/info/overview/snapshots) | 720 | **Port with QCommandLineParser.** Same subcommands, ANSI bars, `--json`. |

### 3.2 Rewritten (Linux syscall layer)

| macOS mechanism | Linux replacement |
|---|---|
| `getattrlistbulk` + packed attr parser (~570 LoC) | `getdents64` + `statx` per entry (`STATX_TYPE\|STATX_SIZE\|STATX_BLOCKS\|STATX_INO\|STATX_NLINK\|STATX_MNT_ID`). Keep the existing readdir-fallback *shape*; it becomes the primary path. Optional perf spike: `io_uring` batched statx. |
| `ATTR_FILE_DATALENGTH` → Logical | `stx_size` |
| `ATTR_FILE_ALLOCSIZE` → Physical | `stx_blocks * 512` |
| `ATTR_CMNEXT_PRIVATESIZE` → Freeable | **Phase 1: dropped** (lens enum keeps 2 entries). Phase 2: FIEMAP extent-overlap engine (§6). |
| `CLONEID/CLONE_REFCNT` (APFS clone families) | Phase 1: hardlinks only via `(stx_dev, stx_ino)` + `stx_nlink`. Phase 2: reflink detection via `FIEMAP` physical extents (btrfs). |
| Sparse detection (`EF_IS_SPARSE`) | `stx_blocks*512 < stx_size` heuristic (same honesty, no flag needed). |
| Compressed (`UF_COMPRESSED`) / dataless (`SF_DATALESS`) | btrfs compression flag via `FS_IOC_GETFLAGS` (`FS_COMPR_FL`). Dataless: not applicable on Linux — flag retired. |
| `statfs` fields (`f_mntonname` etc.) | `statvfs` + parse `/proc/self/mountinfo` (source, fstype, mount point, root). |
| Purgeable space (`volumeAvailableCapacityForImportantUsage`) | Dropped — no Linux concept. Show used/free honestly; btrfs caveat: shared pool noted in overview. |
| Firmlinks (`/usr/share/firmlinks`, `/System/Volumes/Data`) | **Deleted entirely.** Cross-mount protection via `stx_mnt_id` allow-list instead. |
| Full Disk Access probe (TCC canaries) | **Deleted.** Optional `geteuid()==0` hint in overview ("run as root for full visibility"). |
| `diskutil apfs listSnapshots`, `tmutil` | `btrfs subvolume list -s /` via existing Subprocess pattern (guarded: only when root fs is btrfs + tool exists). Parse ro/rw, path. |
| `FileManager.trashItem` | FreeDesktop Trash spec: `gio trash <path>` subprocess (KDE always has gio via glib; fallback manual `~/.local/share/Trash/{files,info}` move + `.trashinfo`). |
| `NSOpenPanel` / `NSWorkspace` | `QFileDialog` (native portal) / `QDesktopServices::openUrl` (reveal in Dolphin). |
| `UserDefaults` (settings) | `QSettings` (INI in `~/.config/asahidisksleuth/`). |
| `OSAllocatedUnfairLock` | `QMutex` / `std::mutex`. |
| SF Symbols | Freedesktop/Breeze icon names (`QIcon::fromTheme`). |

### 3.3 Deleted outright

- `Attributes/` getattrlistbulk machinery, `Firmlinks.swift`, `FDAProbe.swift`,
  APFS snapshot parsers, VM/swap volume special-casing, purgeable-space plumbing,
  xcodegen/Xcode project, macOS CI.

---

## 4. The "truth engine" on Linux — honesty policy

The original's core value: *report what the kernel actually says, flag what it can't know.*
Preserved on Linux as:

| Lens | Source | Notes |
|---|---|---|
| **Logical** | `stx_size` | Same meaning as macOS. |
| **Physical** | `stx_blocks * 512` | Honest per-file. On btrfs, compression makes this *smaller* than logical — a feature, reported as-is. |
| **Freeable** (Phase 2) | FIEMAP extent overlap | Explicitly labeled **estimate** when reflink sharing is involved; hardlink handling stays exact. |

Additional honesty carryovers:
- **Denied dirs:** `EACCES` on `openat`/`statx` → recorded in a permission ledger (cap 2,000),
  flagged in tree + summary, counted in totals as "unknown".
- **btrfs pool caveat:** overview shows "btrfs: all subvolumes share one 227 GiB pool"
  instead of per-subvolume used numbers (which btrfs reports identically — avoid double-count illusion).
- **Hardlinks:** charged once at lowest common ancestor, freeable only when all links in-tree
  (identical logic to GraphBuilder — direct port).
- **Sparse files:** physical < logical, flagged, never inflated.

---

## 5. GUI design (Kirigami — "slick" requirements)

Structure mirrors the SwiftUI app 1:1, translated to KDE idioms:

| SwiftUI view | Kirigami/QML equivalent |
|---|---|
| `DiskSleuthApp` / `RootView` phase switch | `Kirigami.ApplicationWindow`, page stack: welcome → scanning → results |
| `WelcomeView` (boot disk card, capacity bar, scan button) | `Kirigami.CardsLayout` + `Controls.ProgressBar` + btrfs pool note |
| `ScanProgressView` | Inline overlay with live file count / current path (100 ms throttle, same as engine) |
| `DiskOverviewView` donut | QML `Shape`/`ShapePath` arcs, animated |
| `SunburstView` (SwiftUI Canvas, polar hit-test) | **QML `Canvas` (JS paint) or custom `QQuickPaintedItem` (C++)** — decision: start with Canvas for velocity, move to C++ painted item if profiling demands. Same hover/tap geometry, same HSB-by-ring coloring, same "other"-slice collapse (<0.4%). |
| `ResultsView` (HSplitView: sunburst \| tree \| inspector) | `Kirigami.PageRow` or 3-pane `SplitView`; tree = `TreeView` (Qt 6.11 native) backed by C++ `QAbstractItemModel` over `FileGraph` |
| `InspectorView` (truth card, badges, Move to Trash) | Side `Kirigami.OverlaySheet` / panel; Trash via `gio trash` with `Kirigami.PromptDialog` confirm |
| Lens toggle (Logical/Physical) | `Controls.SegmentedButton`-style toggle in toolbar |
| Breadcrumbs | `Kirigami.NavigationTabBar`-style breadcrumb row |
| Reveal in Finder | "Open in Dolphin" → `QDesktopServices::openUrl(QUrl::fromLocalFile(...))` |
| Text scale ⌘+/− | `Ctrl++`/`Ctrl+-` adjusting a scale factor stored in QSettings |
| Menu commands | `KActionCollection`-style global actions + Kirigami actions |

**Look & feel:** Breeze theme by default (free on Plasma), custom accent colors only for the
sunburst palette. `KCrash` wired in. AppStream metainfo + `.desktop` file from day one.

---

## 6. Phase plan with milestones

### Phase 0 — Scaffold (M0)
- `dnf install` deps (§1).
- CMake skeleton: core static lib, empty CLI, Catch2 from system or FetchContent.
- Port `ByteFormat` + tests (smallest module — validates toolchain).
- **Done when:** `cmake -B build -G Ninja && ninja && ctest` green.

### Phase 1 — Linux truth engine (M1)  ← *highest risk, do first*
- `RawEntry`, `DirectoryReader` (openat + getdents64 + statx, O_NOFOLLOW, fd-refcounted
  `DirHandle`), `VisitedSet` on `(mnt_id, ino)`.
- Port `FileGraph`, `NodeFlags` (drop dataless/purgeable/firmlinkSkipped; keep cloned bit
  reserved for Phase 2), `GraphBuilder` (hardlink LCA logic intact, clone path stubbed).
- Port `ScanEngine` + `WorkQueue` on QThreadPool; cancellation → partial graph.
- Fixture tests mirroring the originals: hardlink group, sparse file, deep tree,
  permission-denied dir, symlink loop — assert against `stat(2)` ground truth.
  (Original used clonefile fixtures → deferred to Phase 2; reflink fixture = `cp --reflink=always`.)
- **Done when:** `ctest` green; scan of `/home` completes; node counts match `find /home | wc -l`
  within denied-dir tolerance; RSS bounded under ~2× graph size on a 1M-file tree.

### Phase 2 — CLI parity (M2)
- `scan` (ANSI tree + bars + badges, `--json`), `top`, `info`, `overview` (statvfs capacity,
  btrfs pool caveat, denied-count), `snapshots` (btrfs subvolume list -s).
- Same JSON schema as the Swift version where fields overlap (keeps docs/diffing easy).
- **Done when:** CLI output on a fixture tree matches expected snapshots; `overview` numbers
  match `df -B1` exactly for `/`.

### Phase 3 — Kirigami app MVP (M3)
- Welcome (capacity bar from `statvfs`), scan with live progress, results:
  sunburst + tree + inspector, lens toggle, breadcrumbs, Open in Dolphin, Move to Trash
  with in-memory graph patch (`FileGraph::removing` port).
- QSettings persistence (last root, lens, text scale).
- **Done when:** full manual QA script passes; scan → drill-down → trash round-trip works;
  60 fps hover on sunburst over a 100k-node graph.

### Phase 4 — Polish & packaging (M4)
- AppStream metainfo, `.desktop`, icons (Breeze-style SVG), KCrash, i18n scaffolding (ki18n).
- RPM spec built and tested locally (`rpmbuild -ba`), installs cleanly on this machine.
- README with screenshots; `docs/truth-engine-linux.md` documenting the honesty policy (§4).
- **Done when:** `sudo dnf install ./asahidisksleuth-*.rpm` → app appears in Kickoff and runs.

### Phase 5 (optional) — btrfs Freeable lens
- FIEMAP extent enumeration per file; shared-extent detection via physical-offset hashing
  (jdupes-style); clone-family accounting wired into existing GraphBuilder registries
  (the LCA math is already there — it was built for exactly this).
- UI: third lens option appears only on btrfs roots; labeled "estimate" when reflinks present.
- Fixture: `cp --reflink=always` pair → assert charged-once behavior.
- **Gate:** must not slow non-btrfs scans; FS-type-gated.

---

## 7. Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| statx-per-file slower than getattrlistbulk (1 syscall/entry vs 1/many) | Slower scans | 8 workers hide latency (same as macOS design — it was syscall-latency-bound too). Spike `io_uring` batched statx in Phase 1 only if profiling shows need. |
| QML Canvas sunburst too slow at 100k+ nodes | Janky UI | Layout precomputed in C++; draw only visible arcs (≤ a few hundred). Fallback: `QQuickPaintedItem` with QPainter. |
| btrfs shared-pool accounting confuses users ("used" same everywhere) | Trust erosion | Explicit pool note in UI + docs; per-subvolume qgroups only Phase 5+. |
| Kirigami 6.29 API drift vs online docs | Build friction | Pin to installed KF6 version; docs packages installed locally. |
| Trash spec edge cases (cross-device) | Data-loss perception | Use `gio trash` (handles all cases); confirm dialog always. |

## 8. Estimates (rough, LoC of new C++/QML)

| Component | Est. LoC | Basis |
|---|---|---|
| core/model + core/attrs + core/scan | ~1,800 | From ~1,600 Swift, C++ header tax |
| core/volume + util (trash, format, subprocess) | ~400 | Mostly new, small |
| CLI | ~500 | From 720 Swift, QCommandLineParser is terser |
| App (QML + bridges) | ~1,400 | From 1,268 SwiftUI |
| Tests | ~600 | Mirrors original + fixtures |
| **Total** | **~4,700** | Comparable to original |

## 9. What I need from you to proceed

1. Approval of this plan (or amendments).
2. Permission to run `sudo dnf install` for the Phase 0 dependency list (§1) — requires your password/sudo.
3. Whether to `git init` in `/home/jdenman/asahidisksleuth` (fresh history, recommended) — I'll reference macdisksleuth by reading it, not vendoring it.
