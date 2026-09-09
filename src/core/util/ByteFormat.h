#pragma once

#include <QString>
#include <QtTypes>

namespace ads {

/// Human-readable byte formatting shared by the CLI and the app.
/// Decimal style (matches df -H / most file managers) or binary (IEC) style.
/// Negative values yield an em dash, meaning "unknown".
QString formatBytes(qint64 bytes, bool binary = false);

} // namespace ads
