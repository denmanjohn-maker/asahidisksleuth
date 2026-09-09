#include <QTest>

#include "util/ByteFormat.h"

// Contract ported from DiskSleuthKit/Format/ByteCount.swift:
//  - negative -> em dash
//  - < 1000 (decimal) / < 1024 (binary) -> "N B"
//  - value >= 100 -> 0 decimals; >= 10 -> 1; else 2
class TestByteFormat : public QObject
{
    Q_OBJECT

private slots:
    void negativesAreUnknown()
    {
        QCOMPARE(ads::formatBytes(-1), QStringLiteral("—"));
        QCOMPARE(ads::formatBytes(INT64_MIN), QStringLiteral("—"));
    }

    void plainBytes()
    {
        QCOMPARE(ads::formatBytes(0), QStringLiteral("0 B"));
        QCOMPARE(ads::formatBytes(512), QStringLiteral("512 B"));
        QCOMPARE(ads::formatBytes(999), QStringLiteral("999 B"));
    }

    void decimal()
    {
        QCOMPARE(ads::formatBytes(1000), QStringLiteral("1.00 KB"));
        QCOMPARE(ads::formatBytes(1500), QStringLiteral("1.50 KB"));
        QCOMPARE(ads::formatBytes(10'500), QStringLiteral("10.5 KB"));
        QCOMPARE(ads::formatBytes(150'000), QStringLiteral("150 KB"));
        QCOMPARE(ads::formatBytes(498'100'000'000LL), QStringLiteral("498 GB"));
        QCOMPARE(ads::formatBytes(1'500'000'000LL), QStringLiteral("1.50 GB"));
    }

    void binary()
    {
        QCOMPARE(ads::formatBytes(1024, true), QStringLiteral("1.00 KiB"));
        QCOMPARE(ads::formatBytes(500'000'000'000LL, true), QStringLiteral("466 GiB"));
        QCOMPARE(ads::formatBytes(1536, true), QStringLiteral("1.50 KiB"));
    }
};

QTEST_GUILESS_MAIN(TestByteFormat)
#include "test_byteformat.moc"
