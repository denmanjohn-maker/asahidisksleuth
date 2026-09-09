#include "ScanRunner.h"
#include "Style.h"
#include "TreePrinter.h"

#include "../core/util/ByteFormat.h"
#include "../core/volume/VolumeInfo.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTextStream>

#include <algorithm>
#include <functional>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

using namespace ads;
using namespace ads::cli;

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

QLocale loc;

QString padded(const QString &s, int width)
{
    return s.leftJustified(width, u' ');
}

bool addCommonOpts(QCommandLineParser &p)
{
    p.addOption({{u"j"_s, u"json"_s}, u"Emit machine-readable JSON."_s});
    p.addOption({u"no-color"_s, u"Disable ANSI colors."_s});
    p.addOption({u"si"_s, u"Use binary units (GiB) instead of decimal (GB)."_s});
    return true;
}

bool wantsJson(const QCommandLineParser &p) { return p.isSet(u"json"_s); }
bool noColor(const QCommandLineParser &p) { return p.isSet(u"no-color"_s); }
bool binaryUnits(const QCommandLineParser &p) { return p.isSet(u"si"_s); }

SizeLens parseLens(const QString &arg, bool *ok)
{
    const QString a = arg.toLower();
    if (a == u"logical" || a == u"l")
        return SizeLens::Logical;
    if (a == u"physical" || a == u"p")
        return SizeLens::Physical;
    if (a == u"unique" || a == u"freeable" || a == u"u" || a == u"f")
        return SizeLens::Unique;
    *ok = false;
    return SizeLens::Physical;
}

QString lensName(SizeLens lens)
{
    switch (lens) {
    case SizeLens::Logical:
        return u"logical"_s;
    case SizeLens::Physical:
        return u"physical"_s;
    case SizeLens::Unique:
        return u"unique"_s;
    }
    return {};
}

QString kindName(NodeKind kind)
{
    switch (kind) {
    case NodeKind::Directory:
        return u"directory"_s;
    case NodeKind::Symlink:
        return u"symlink"_s;
    case NodeKind::File:
        return u"file"_s;
    case NodeKind::Other:
        return u"other"_s;
    }
    return {};
}

// ---------------------------------------------------------------------------
// scan
// ---------------------------------------------------------------------------

int cmdScan(QCommandLineParser &p)
{
    p.setApplicationDescription(u"Scan a folder or volume and show where the space really went."_s);
    addCommonOpts(p);
    p.addOption({u"depth"_s, u"Tree depth to display."_s, u"n"_s, u"2"_s});
    p.addOption({u"top"_s, u"Children shown per directory."_s, u"n"_s, u"10"_s});
    p.addOption({u"size-mode"_s, u"Lens: logical, physical, or unique (freeable)."_s, u"lens"_s,
                 u"physical"_s});
    p.addOption({u"cross-file-systems"_s, u"Traverse onto other mounts (default: stay on the starting volume)."_s});
    p.addPositionalArgument(u"path"_s, u"Directory to scan (default: current directory)."_s,
                            u"[path]"_s);
    p.process(*QCoreApplication::instance());

    const QString target = p.positionalArguments().value(0, u"."_s);
    const bool binary = binaryUnits(p);
    auto fmt = [&](qint64 b) { return formatBytes(b, binary); };

    bool lensOk = true;
    const SizeLens lens = parseLens(p.value(u"size-mode"_s), &lensOk);
    if (!lensOk) {
        QTextStream(stderr) << u"Invalid --size-mode: %1\n"_s.arg(p.value(u"size-mode"_s));
        return 2;
    }

    ScanOptions options;
    options.crossVolumes = p.isSet(u"cross-file-systems"_s);

    QString error;
    const ScanResult result = runScan(target, options, &error);
    if (!error.isEmpty()) {
        QTextStream(stderr) << error << u'\n';
        return 1;
    }
    const FileGraph &graph = result.graph;
    const ScanSummary &summary = graph.summary;

    if (wantsJson(p)) {
        std::function<QJsonObject(NodeID, int)> nodeJson = [&](NodeID node, int depth) {
            QJsonObject o;
            const Sizes sizes = graph.sizes(node);
            const NodeFlags flags = graph.flags(node);
            o[u"name"_s] = depth == 0 ? graph.rootPath : graph.name(node);
            o[u"kind"_s] = kindName(flags.kind());
            o[u"logical"_s] = sizes.logical;
            o[u"physical"_s] = sizes.physical;
            o[u"freeable"_s] = sizes.unique;
            QJsonArray badges;
            if (flags.sparse())
                badges.append(u"sparse"_s);
            if (flags.hardlinked())
                badges.append(u"hardlink"_s);
            if (flags.compressed())
                badges.append(u"compressed"_s);
            if (flags.accessDenied())
                badges.append(u"denied"_s);
            if (flags.otherVolume())
                badges.append(u"otherMount"_s);
            if (flags.duplicate())
                badges.append(u"duplicate"_s);
            if (flags.externalLinks())
                badges.append(u"sharedOutside"_s);
            o[u"badges"_s] = badges;
            const int maxDepth = p.value(u"depth"_s).toInt();
            if (flags.kind() == NodeKind::Directory && depth < maxDepth) {
                const QVector<NodeID> all = graph.children(node);
                QVector<NodeID> ranked = all;
                std::sort(ranked.begin(), ranked.end(), [&](NodeID a, NodeID b) {
                    return graph.size(a, lens) > graph.size(b, lens);
                });
                const int top = p.value(u"top"_s).toInt();
                QJsonArray kids;
                for (int i = 0; i < ranked.size() && i < top; ++i)
                    kids.append(nodeJson(ranked[i], depth + 1));
                o[u"children"_s] = kids;
                if (all.size() > top)
                    o[u"collapsedChildren"_s] = all.size() - top;
            }
            return o;
        };

        QJsonObject payload;
        payload[u"schemaVersion"_s] = 1;
        payload[u"root"_s] = graph.rootPath;
        payload[u"files"_s] = summary.fileCount;
        payload[u"directories"_s] = summary.directoryCount;
        payload[u"totalLogical"_s] = summary.totalLogical;
        payload[u"totalPhysical"_s] = summary.totalPhysical;
        payload[u"totalFreeable"_s] = summary.totalUnique;
        payload[u"hardlinkedFiles"_s] = summary.hardlinkedFileCount;
        payload[u"sparseFiles"_s] = summary.sparseFileCount;
        payload[u"deniedDirectories"_s] = summary.deniedDirectoryCount;
        payload[u"partial"_s] = summary.partial;
        payload[u"wallSeconds"_s] = summary.wallSeconds;
        payload[u"tree"_s] = nodeJson(graph.root(), 0);
        out() << QJsonDocument(payload).toJson(QJsonDocument::Indented).constData() << u'\n';
        return 0;
    }

    const Style style(noColor(p));
    const QString counts = loc.toString(summary.fileCount) + u" files · "
        + loc.toString(summary.directoryCount) + u" folders";
    const QString timing = QString::number(summary.wallSeconds, 'f', 1) + u's';
    out() << style.bold(u"asahidisksleuth"_s)
          << style.dim(u" · "_s + counts + u" · "_s + timing
                       + (summary.partial ? u" · PARTIAL (cancelled)"_s : QString()))
          << u'\n';
    out() << style.dim(u"logical "_s + fmt(summary.totalLogical) + u" · physical "_s
                           + fmt(summary.totalPhysical) + u" · freeable now "_s
                           + fmt(summary.totalUnique))
          << u'\n';

    QStringList notes;
    if (summary.hardlinkedFileCount > 0)
        notes.append(loc.toString(summary.hardlinkedFileCount) + u" hardlinks ⛓"_s);
    if (summary.sparseFileCount > 0)
        notes.append(loc.toString(summary.sparseFileCount) + u" sparse ▤"_s);
    if (!notes.isEmpty())
        out() << style.dim(notes.join(u" · "_s)) << u'\n';
    out() << u'\n';

    out().flush();
    TreePrinter(graph, lens, style, binary, p.value(u"depth"_s).toInt(),
                p.value(u"top"_s).toInt())
        .printTree(out());

    if (summary.deniedDirectoryCount > 0) {
        out() << u'\n'
              << style.yellow(
                     QStringLiteral("⚠ %1 folder%2 could not be read — totals undercount.")
                         .arg(summary.deniedDirectoryCount)
                         .arg(summary.deniedDirectoryCount == 1 ? u""_s : u"s"_s))
              << u'\n';
    }
    return 0;
}

// ---------------------------------------------------------------------------
// top
// ---------------------------------------------------------------------------

int cmdTop(QCommandLineParser &p)
{
    p.setApplicationDescription(u"The biggest files or folders under a path, with honest sizes."_s);
    addCommonOpts(p);
    p.addOption({{u"n"_s, u"number"_s}, u"How many items to show."_s, u"n"_s, u"25"_s});
    p.addOption({u"dirs"_s, u"Rank folders instead of files."_s});
    p.addOption({u"size-mode"_s, u"Lens: logical, physical, or unique (freeable)."_s, u"lens"_s,
                 u"physical"_s});
    p.addPositionalArgument(u"path"_s, u"Directory to scan (default: current directory)."_s,
                            u"[path]"_s);
    p.process(*QCoreApplication::instance());

    const QString target = p.positionalArguments().value(0, u"."_s);
    const bool binary = binaryUnits(p);
    auto fmt = [&](qint64 b) { return formatBytes(b, binary); };

    bool lensOk = true;
    const SizeLens lens = parseLens(p.value(u"size-mode"_s), &lensOk);
    if (!lensOk)
        return 2;

    QString error;
    const ScanResult result = runScan(target, {}, &error);
    if (!error.isEmpty()) {
        QTextStream(stderr) << error << u'\n';
        return 1;
    }
    const FileGraph &graph = result.graph;
    const bool rankDirs = p.isSet(u"dirs"_s);

    QVector<NodeID> candidates;
    candidates.reserve(graph.nodeCount() / 4);
    for (qint32 raw = 0; raw < graph.nodeCount(); ++raw) {
        const NodeID node{raw};
        const NodeKind kind = graph.kind(node);
        if (rankDirs) {
            if (kind == NodeKind::Directory && raw != 0)
                candidates.append(node);
        } else if (kind == NodeKind::File) {
            candidates.append(node);
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [&](NodeID a, NodeID b) { return graph.size(a, lens) > graph.size(b, lens); });
    const int number = p.value(u"number"_s).toInt();
    candidates.resize(std::min<int>(number, candidates.size()));

    if (wantsJson(p)) {
        QJsonArray items;
        for (const NodeID node : candidates) {
            const Sizes s = graph.sizes(node);
            QJsonObject row;
            row[u"path"_s] = graph.path(node);
            row[u"logical"_s] = s.logical;
            row[u"physical"_s] = s.physical;
            row[u"freeable"_s] = s.unique;
            items.append(row);
        }
        QJsonObject payload;
        payload[u"schemaVersion"_s] = 1;
        payload[u"root"_s] = graph.rootPath;
        payload[u"rankedBy"_s] = lensName(lens);
        payload[u"items"_s] = items;
        out() << QJsonDocument(payload).toJson(QJsonDocument::Indented).constData() << u'\n';
        return 0;
    }

    const Style style(noColor(p));
    out() << style.dim(u"  "_s + padded(u"LOGICAL"_s, 11) + padded(u"PHYSICAL"_s, 11)
                           + padded(u"FREEABLE"_s, 11) + u"PATH"_s)
          << u'\n';
    for (const NodeID node : candidates) {
        const Sizes s = graph.sizes(node);
        out() << u"  "_s << padded(fmt(s.logical), 11)
              << style.bold(padded(fmt(s.physical), 11))
              << style.green(padded(fmt(s.unique), 11)) << graph.path(node)
              << TreePrinter::badges(graph.flags(node), style) << u'\n';
    }
    if (graph.summary.deniedDirectoryCount > 0)
        out() << style.yellow(QStringLiteral("⚠ %1 folders unreadable — see `asahidisksleuth scan` for details.")
                                  .arg(graph.summary.deniedDirectoryCount))
              << u'\n';
    return 0;
}

// ---------------------------------------------------------------------------
// overview
// ---------------------------------------------------------------------------

int cmdOverview(QCommandLineParser &p)
{
    p.setApplicationDescription(
        u"Why is my disk full? Capacity, filesystem, and btrfs snapshots."_s);
    addCommonOpts(p);
    p.addPositionalArgument(u"volume"_s, u"Path on the volume to inspect (default: /)."_s,
                            u"[volume]"_s);
    p.process(*QCoreApplication::instance());

    const QString target = p.positionalArguments().value(0, u"/"_s);
    const bool binary = binaryUnits(p);
    auto fmt = [&](qint64 b) { return formatBytes(b, binary); };

    VolumeInfo vol;
    QString error;
    if (!VolumeService::identity(target, &vol, &error)) {
        QTextStream(stderr) << error << u'\n';
        return 1;
    }
    const QVector<BtrfsSnapshot> snapshots = VolumeService::btrfsSnapshots(target);

    if (wantsJson(p)) {
        QJsonObject payload;
        payload[u"schemaVersion"_s] = 1;
        payload[u"mountPoint"_s] = vol.mountPoint;
        payload[u"device"_s] = vol.device;
        payload[u"filesystem"_s] = vol.fsType;
        payload[u"totalCapacity"_s] = vol.totalBytes;
        payload[u"usedBytes"_s] = vol.usedBytes;
        payload[u"freeNow"_s] = vol.availableBytes;
        payload[u"readOnly"_s] = vol.readOnly;
        payload[u"localSnapshots"_s] = snapshots.size();
        out() << QJsonDocument(payload).toJson(QJsonDocument::Indented).constData() << u'\n';
        return 0;
    }

    const Style style(noColor(p));
    const QString title = vol.mountPoint == u"/" ? u"Root filesystem"_s : vol.mountPoint;
    out() << style.bold(title) << style.dim(u"  "_s + vol.device + u" · "_s + vol.fsType) << u'\n'
          << u'\n';

    auto row = [&](const QString &label, const QString &value, const QString &note = QString()) {
        out() << u"  "_s << padded(label, 28) << style.bold(padded(value, 12)) << style.dim(note)
              << u'\n';
    };
    row(u"Capacity"_s, fmt(vol.totalBytes));
    row(u"Used"_s, fmt(vol.usedBytes));
    row(u"Available"_s, fmt(vol.availableBytes));
    if (vol.isBtrfs)
        row(u"Note"_s, QString(),
            u"btrfs: all subvolumes share this pool — per-folder used space adds up to less"_s);
    if (!snapshots.isEmpty())
        row(u"btrfs snapshots"_s, QString::number(snapshots.size()),
            u"see `asahidisksleuth snapshots`"_s);
    if (vol.readOnly)
        row(u"Read-only"_s, u"yes"_s);
    return 0;
}

// ---------------------------------------------------------------------------
// snapshots
// ---------------------------------------------------------------------------

int cmdSnapshots(QCommandLineParser &p)
{
    p.setApplicationDescription(u"btrfs snapshots — the invisible tenant holding your freed space."_s);
    addCommonOpts(p);
    p.addPositionalArgument(u"volume"_s, u"Path on the volume to inspect (default: /)."_s,
                            u"[volume]"_s);
    p.process(*QCoreApplication::instance());

    const QString target = p.positionalArguments().value(0, u"/"_s);
    VolumeInfo vol;
    QString error;
    if (!VolumeService::identity(target, &vol, &error)) {
        QTextStream(stderr) << error << u'\n';
        return 1;
    }
    const QVector<BtrfsSnapshot> snapshots = VolumeService::btrfsSnapshots(target);

    if (wantsJson(p)) {
        QJsonArray arr;
        for (const BtrfsSnapshot &s : snapshots) {
            QJsonObject o;
            o[u"path"_s] = s.path;
            o[u"id"_s] = s.id;
            o[u"readOnly"_s] = s.readOnly;
            arr.append(o);
        }
        QJsonObject payload;
        payload[u"schemaVersion"_s] = 1;
        payload[u"volume"_s] = vol.mountPoint;
        payload[u"snapshots"_s] = arr;
        out() << QJsonDocument(payload).toJson(QJsonDocument::Indented).constData() << u'\n';
        return 0;
    }

    const Style style(noColor(p));
    if (snapshots.isEmpty()) {
        out() << u"No btrfs snapshots found under "_s << vol.mountPoint << u".\n"_s;
        if (!vol.isBtrfs)
            out() << style.dim(u"("_s + vol.fsType + u" — not a btrfs volume)"_s) << u'\n';
        return 0;
    }
    out() << style.bold(QStringLiteral("%1 btrfs snapshot%2 under %3")
                            .arg(snapshots.size())
                            .arg(snapshots.size() == 1 ? u""_s : u"s"_s, vol.mountPoint))
          << u'\n'
          << u'\n';
    for (const BtrfsSnapshot &s : snapshots)
        out() << u"  ◷ "_s << style.cyan(s.path) << style.dim(u"  id "_s + QString::number(s.id))
              << u'\n';
    out() << u'\n'
          << style.dim(u"Deleted files referenced by snapshots are not freed until snapshots are removed."_s)
          << u'\n'
          << style.dim(u"  btrfs subvolume delete <path>   (remove a snapshot)"_s) << u'\n';
    return 0;
}

// ---------------------------------------------------------------------------
// info
// ---------------------------------------------------------------------------

int cmdInfo(QCommandLineParser &p)
{
    p.setApplicationDescription(
        u"Truth card for one file: real sizes and what deleting frees."_s);
    addCommonOpts(p);
    p.addPositionalArgument(u"path"_s, u"Path to inspect."_s);
    p.process(*QCoreApplication::instance());

    const QStringList args = p.positionalArguments();
    if (args.isEmpty()) {
        QTextStream(stderr) << u"Missing path.\n"_s;
        return 2;
    }
    const QString target = args.first();
    const bool binary = binaryUnits(p);
    auto fmt = [&](qint64 b) { return formatBytes(b, binary); };

    struct statx stx {};
    if (::statx(AT_FDCWD, QFile::encodeName(target).constData(), AT_SYMLINK_NOFOLLOW,
                STATX_TYPE | STATX_SIZE | STATX_BLOCKS | STATX_NLINK, &stx)
        != 0) {
        QTextStream(stderr) << u"Cannot read "_s << target << u": "_s
                            << QString::fromUtf8(strerror(errno)) << u'\n';
        return 1;
    }

    const bool isDir = (stx.stx_mode & S_IFMT) == S_IFDIR;
    const bool isSymlink = (stx.stx_mode & S_IFMT) == S_IFLNK;
    const qint64 logical = isDir ? 0 : qint64(stx.stx_size);
    const qint64 physical = (stx.stx_mask & STATX_BLOCKS) ? qint64(stx.stx_blocks) * 512 : 0;
    const qint64 freeable = stx.stx_nlink > 1 && !isDir ? 0 : physical;

    VolumeInfo vol;
    VolumeService::identity(target, &vol);

    const QString kind = isDir ? u"directory"_s : isSymlink ? u"symlink"_s : u"file"_s;
    const bool sparse = logical > 0 && physical < logical;

    if (wantsJson(p)) {
        QJsonObject payload;
        payload[u"schemaVersion"_s] = 1;
        payload[u"path"_s] = target;
        payload[u"kind"_s] = kind;
        payload[u"volume"_s] = vol.mountPoint;
        payload[u"filesystem"_s] = vol.fsType;
        payload[u"logical"_s] = logical;
        payload[u"physical"_s] = physical;
        payload[u"freeableNow"_s] = freeable;
        payload[u"linkCount"_s] = qint64(stx.stx_nlink);
        payload[u"isSparse"_s] = sparse;
        out() << QJsonDocument(payload).toJson(QJsonDocument::Indented).constData() << u'\n';
        return 0;
    }

    const Style style(noColor(p));
    QStringList badges;
    if (sparse)
        badges.append(u"▤ sparse"_s);
    if (stx.stx_nlink > 1 && !isDir)
        badges.append(QStringLiteral("⛓ hardlink ×%1").arg(stx.stx_nlink));

    out() << style.boldBlue(target) << u'\n';
    QString meta = kind + u" on "_s + vol.fsType + u" ("_s + vol.mountPoint + u')';
    if (!badges.isEmpty())
        meta += u"   "_s + badges.join(u"  "_s);
    out() << style.dim(meta) << u'\n' << u'\n';

    if (isDir) {
        out() << u"Directories carry no size of their own — run:\n"_s;
        out() << style.cyan(u"  asahidisksleuth scan "_s + target) << u'\n';
        return 0;
    }

    auto row = [&](const QString &label, const QString &value, const QString &note) {
        out() << u"  "_s << padded(label, 14) << style.bold(padded(value, 12)) << style.dim(note)
              << u'\n';
    };
    row(u"logical"_s, fmt(logical), u"what the file claims to be"_s);
    row(u"physical"_s, fmt(physical), u"what the disk actually holds"_s);
    row(u"freeable now"_s, fmt(freeable), u"what deleting this really gives back"_s);
    out() << u'\n';

    if (stx.stx_nlink > 1)
        out() << u"  "_s << style.yellow(u"⛓"_s)
              << QStringLiteral(" %1 hard links share this content — "
                                "deleting one path frees nothing until all links are gone.")
                     .arg(stx.stx_nlink)
              << u'\n';
    if (sparse)
        out() << u"  "_s << style.cyan(u"▤"_s)
              << u" Sparse file: holes are not allocated, so physical < logical.\n"_s;

    out() << u'\n'
          << u"  "_s << style.bold(style.green(u"Deleting frees ~"_s + fmt(freeable) + u" now"_s))
          << u'\n';
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("asahidisksleuth"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0-dev"));
    loc = QLocale::system();

    const QStringList args = app.arguments();
    const QString command = args.value(1);

    // Peel off the subcommand before handing the rest to each parser.
    const bool looksLikeCommand = command == u"scan" || command == u"top" || command == u"info"
        || command == u"overview" || command == u"snapshots";

    if (!looksLikeCommand) {
        QCommandLineParser p;
        p.setApplicationDescription(
            u"Honest disk usage analysis for Linux.\n\n"
            "Commands:\n"
            "  scan       Scan a folder and show where the space really went\n"
            "  top        The biggest files or folders under a path\n"
            "  info       Truth card for one file\n"
            "  overview   Capacity, filesystem, and snapshots for a volume\n"
            "  snapshots  List btrfs snapshots\n"_s);
        p.addHelpOption();
        p.addVersionOption();
        p.process(app);
        p.showHelp(command.isEmpty() || command == u"--help" || command == u"-h" ? 0 : 2);
    }

    // Rebuild argv without the subcommand for per-command parsers.
    QVector<QByteArray> rawArgs;
    rawArgs.append(args.first().toUtf8());
    for (const QString &a : args.mid(2))
        rawArgs.append(a.toUtf8());
    QVector<char *> rawArgv;
    for (QByteArray &a : rawArgs)
        rawArgv.append(a.data());
    int rawArgc = rawArgv.size();
    QCoreApplication cmdApp(rawArgc, rawArgv.data());

    QCommandLineParser p;
    p.addHelpOption();
    if (command == u"scan")
        return cmdScan(p);
    if (command == u"top")
        return cmdTop(p);
    if (command == u"info")
        return cmdInfo(p);
    if (command == u"overview")
        return cmdOverview(p);
    if (command == u"snapshots")
        return cmdSnapshots(p);
    return 2;
}
