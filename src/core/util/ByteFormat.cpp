#include "ByteFormat.h"

#include <array>

namespace ads {

QString formatBytes(qint64 bytes, bool binary)
{
    if (bytes < 0)
        return QStringLiteral("—");

    const double unit = binary ? 1024.0 : 1000.0;
    static constexpr std::array decimalSuffixes{"B", "KB", "MB", "GB", "TB", "PB"};
    static constexpr std::array binarySuffixes{"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    const auto &suffixes = binary ? binarySuffixes : decimalSuffixes;

    double value = static_cast<double>(bytes);
    size_t index = 0;
    while (value >= unit && index < suffixes.size() - 1) {
        value /= unit;
        ++index;
    }

    if (index == 0)
        return QStringLiteral("%1 B").arg(bytes);

    const int digits = value >= 100.0 ? 0 : (value >= 10.0 ? 1 : 2);
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', digits)
        .arg(QString::fromLatin1(suffixes[index]));
}

} // namespace ads
