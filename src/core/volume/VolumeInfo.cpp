#include "VolumeInfo.h"

#include <QFile>
#include <QProcess>
#include <QTextStream>

#include <sys/statvfs.h>

namespace ads::VolumeService {

namespace {

    /// Longest mount point in /proc/self/mountinfo that is a prefix of `path`.
    struct MountEntry {
        QString mountPoint;
        QString fsType;
        QString device;
        bool readOnly;
    };

    QVector<MountEntry> readMounts()
    {
        QVector<MountEntry> out;
        QFile f(QStringLiteral("/proc/self/mountinfo"));
        if (!f.open(QIODevice::ReadOnly))
            return out;
        // mountinfo: id parent major:minor root mountpoint options - fstype source [super opts]
        // NB: read the whole file at once — procfs reports size 0, which makes
        // QTextStream::readLine() return nothing.
        const QByteArray all = f.readAll();
        const QList<QByteArray> lines = all.split('\n');
        for (const QByteArray &lineBytes : lines) {
            if (lineBytes.isEmpty())
                continue;
            const QString line = QString::fromUtf8(lineBytes);
            const QStringList parts = line.split(u' ');
            if (parts.size() < 10)
                continue;
            MountEntry e;
            e.mountPoint = parts[4];
            e.mountPoint.replace(QStringLiteral("\\040"), QStringLiteral(" ")); // octal-escaped space
            e.readOnly = !parts[5].split(u',').contains(QStringLiteral("rw"));
            const int dash = parts.indexOf(QStringLiteral("-"));
            if (dash < 0 || dash + 2 >= parts.size())
                continue;
            e.fsType = parts[dash + 1];
            e.device = parts[dash + 2];
            out.append(e);
        }
        return out;
    }

    bool startsWithMount(const QString &path, const QString &mount)
    {
        if (mount == u"/")
            return true;
        return path == mount || path.startsWith(mount + u'/');
    }

} // namespace

bool identity(const QString &path, VolumeInfo *out, QString *error)
{
    if (!out)
        return false;

    struct statvfs vfs {};
    if (statvfs(QFile::encodeName(path).constData(), &vfs) != 0) {
        if (error)
            *error = QStringLiteral("statvfs failed for %1").arg(path);
        return false;
    }

    const QVector<MountEntry> mounts = readMounts();
    const MountEntry *best = nullptr;
    for (const MountEntry &m : mounts) {
        if (!startsWithMount(path, m.mountPoint))
            continue;
        if (!best || m.mountPoint.size() > best->mountPoint.size())
            best = &m;
    }

    out->mountPoint = best ? best->mountPoint : QStringLiteral("/");
    out->fsType = best ? best->fsType : QStringLiteral("unknown");
    out->device = best ? best->device : QString();
    out->readOnly = best ? best->readOnly : false;
    out->totalBytes = qint64(vfs.f_blocks) * qint64(vfs.f_frsize);
    out->freeBytes = qint64(vfs.f_bfree) * qint64(vfs.f_frsize);
    out->availableBytes = qint64(vfs.f_bavail) * qint64(vfs.f_frsize);
    out->usedBytes = out->totalBytes - out->freeBytes;
    out->isBtrfs = out->fsType == u"btrfs";
    return true;
}

QVector<BtrfsSnapshot> btrfsSnapshots(const QString &path)
{
    QVector<BtrfsSnapshot> out;
    VolumeInfo info;
    if (!identity(path, &info) || !info.isBtrfs)
        return out;

    // `btrfs subvolume list -s` lists snapshot subvolumes only.
    QProcess proc;
    proc.start(QStringLiteral("btrfs"),
               {QStringLiteral("subvolume"), QStringLiteral("list"), QStringLiteral("-s"),
                info.mountPoint});
    if (!proc.waitForFinished(5000) || proc.exitStatus() != QProcess::NormalExit
        || proc.exitCode() != 0)
        return out;

    // Line shape: "ID 256 gen 123 top level 5 path @snapshots/2026-09-01"
    const QList<QByteArray> lines = proc.readAllStandardOutput().split('\n');
    for (const QByteArray &lineBytes : lines) {
        if (lineBytes.isEmpty())
            continue;
        const QString line = QString::fromUtf8(lineBytes);
        const int idPos = line.indexOf(QStringLiteral("ID "));
        const int pathPos = line.indexOf(QStringLiteral(" path "));
        if (idPos < 0 || pathPos < 0)
            continue;
        BtrfsSnapshot snap;
        snap.id = line.mid(idPos + 3, line.indexOf(u' ', idPos + 3) - (idPos + 3)).toLongLong();
        snap.path = line.mid(pathPos + 6);
        snap.readOnly = true;
        out.append(snap);
    }
    return out;
}

} // namespace ads::VolumeService
