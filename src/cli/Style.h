#pragma once

#include <QString>

#include <unistd.h>

namespace ads::cli {

/// Minimal ANSI styling, enabled only when writing to a TTY (or forced off).
class Style
{
public:
    explicit Style(bool noColor, int fd = STDOUT_FILENO)
    {
        const bool envNoColor = !qEnvironmentVariableIsEmpty("NO_COLOR");
        m_enabled = !noColor && !envNoColor && ::isatty(fd) == 1;
    }

    QString bold(const QString &t) const { return wrap(t, QStringLiteral("1")); }
    QString dim(const QString &t) const { return wrap(t, QStringLiteral("2")); }
    QString cyan(const QString &t) const { return wrap(t, QStringLiteral("36")); }
    QString green(const QString &t) const { return wrap(t, QStringLiteral("32")); }
    QString yellow(const QString &t) const { return wrap(t, QStringLiteral("33")); }
    QString boldBlue(const QString &t) const { return wrap(t, QStringLiteral("1;34")); }

private:
    QString wrap(const QString &t, const QString &code) const
    {
        if (!m_enabled)
            return t;
        return QStringLiteral("\x1b[%1m%2\x1b[0m").arg(code, t);
    }

    bool m_enabled;
};

} // namespace ads::cli
