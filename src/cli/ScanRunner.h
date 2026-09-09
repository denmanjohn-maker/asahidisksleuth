#pragma once

#include "../core/scan/ScanEngine.h"
#include "../core/util/ByteFormat.h"

#include <QLocale>
#include <QTextStream>
#include <QThread>

#include <unistd.h>

namespace ads::cli {

/// Runs a scan with a live stderr progress line (TTY only) and returns the result.
inline ScanResult runScan(const QString &path, const ScanOptions &options, QString *error)
{
    auto session = ScanEngine::scan(path, options, error);
    if (!session)
        return {};

    const bool showProgress = ::isatty(STDERR_FILENO) == 1;
    QTextStream err(stderr);
    static const char *spinner[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    int tick = 0;
    while (!session->isFinished()) {
        if (showProgress) {
            const ScanProgress p = session->progress();
            QString path = p.currentPath;
            if (path.size() > 60)
                path = u"…" + path.right(59);
            err << "\r\x1b[K" << spinner[tick++ % 10] << u' '
                << QLocale().toString(p.filesSeen) << " files · "
                << formatBytes(p.physicalBytes) << " physical · " << path;
            err.flush();
        }
        QThread::msleep(100);
    }
    if (showProgress) {
        err << "\r\x1b[K";
        err.flush();
    }
    return session->wait();
}

} // namespace ads::cli
