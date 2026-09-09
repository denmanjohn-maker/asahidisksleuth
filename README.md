# asahidisksleuth

Honest disk usage analysis for Linux, built for Asahi Fedora (KDE Plasma).
A port of [macdisksleuth](https://github.com/denmanjohn-maker/macdisksleuth) (macOS/APFS)
to C++23 · Qt 6 · Kirigami 6.

## The honesty model

Two size lenses, both read straight from the kernel:

| Lens | Source | Meaning |
|---|---|---|
| **Logical** | `statx.stx_size` | What the file claims to be |
| **Physical** | `statx.stx_blocks × 512` | What the disk actually holds |

Hardlinks are charged **once**, at the lowest common ancestor of the link group —
rollups never multi-count shared content. Sparse files and btrfs compression report
physical < logical as real savings. Unreadable directories are recorded (never
silently skipped) and totals are flagged as undercounting.

A third "Freeable" lens is planned (Phase 2 of the engine): a btrfs FIEMAP-based
approximation of what deleting reflink-shared content actually frees.

## Components

- **`asahidisksleuth`** — CLI: `scan`, `top`, `info`, `overview`, `snapshots`
- **`asahidisksleuth-app`** — Kirigami GUI: live scan progress, interactive
  4-ring sunburst, tree view, inspector with per-item "freeable now", Move to Trash

## Build

```sh
sudo dnf install gcc-c++ cmake ninja-build extra-cmake-modules \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel \
  kf6-kirigami-devel kf6-kcoreaddons-devel kf6-ki18n-devel kf6-kcrash-devel

cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## CLI usage

```sh
asahidisksleuth scan /home --depth 2 --top 10     # tree with usage bars
asahidisksleuth top /home -n 25                   # biggest files
asahidisksleuth top /home --dirs                  # biggest folders
asahidisksleuth info ~/bigfile.iso                # truth card for one file
asahidisksleuth overview /                        # capacity + btrfs pool note
asahidisksleuth snapshots /                       # btrfs snapshots
asahidisksleuth scan /home --json                 # machine-readable
```

All commands support `--json`, `--no-color`, and `--si` (binary units).

## GUI

```sh
./build/src/app/asahidisksleuth-app
```

Welcome → pick Home / Folder / `/` → live progress → results:
sunburst (click to drill, hover for sizes) · child tree · inspector with
Open in Dolphin and Move to Trash (graph updates in memory, no rescan needed).
Toggle Physical/Logical lens in the toolbar.
