#include <QTest>

#include "model/FileGraph.h"
#include "scan/ScanEngine.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <sys/stat.h>
#include <unistd.h>

using namespace ads;

// Fixture tests asserting engine numbers against kernel ground truth,
// mirroring DiskSleuthKit's TruthAccountingTests philosophy.
class TestScan : public QObject
{
    Q_OBJECT

private:
    static bool writeFile(const QString &path, qint64 size)
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        QByteArray chunk(4096, 'x');
        while (size > 0) {
            const qint64 n = std::min(size, qint64(chunk.size()));
            if (f.write(chunk.constData(), n) != n)
                return false;
            size -= n;
        }
        return true;
    }

    static ScanResult scanSync(const QString &path)
    {
        QString error;
        auto session = ScanEngine::scan(path, {}, &error);
        if (!session)
            return ScanResult{FileGraph{}, error};
        return session->wait();
    }

private slots:
    void simpleTree()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("sub/deep")));
        QVERIFY(writeFile(tmp.filePath(QStringLiteral("a.bin")), 10000));
        QVERIFY(writeFile(tmp.filePath(QStringLiteral("sub/b.bin")), 20000));
        QVERIFY(writeFile(tmp.filePath(QStringLiteral("sub/deep/c.bin")), 30000));

        const ScanResult result = scanSync(tmp.path());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        const FileGraph &g = result.graph;
        QCOMPARE(g.nodeCount(), 6); // root + a + sub + b + deep + c
        QCOMPARE(g.summary.fileCount, 3);
        QCOMPARE(g.summary.directoryCount, 2); // sub, deep (root is not counted)

        // Rollup: root logical == sum of file sizes.
        QCOMPARE(g.summary.totalLogical, 60000);
        QCOMPARE(g.sizes(g.root()).logical, 60000);

        // Path reconstruction.
        const auto rootKids = g.children(g.root());
        QCOMPARE(rootKids.size(), 2);
        // Children sorted physical-desc: sub/ (30k+20k) first, a.bin second.
        QCOMPARE(g.name(rootKids[0]), QStringLiteral("sub"));
        QCOMPARE(g.path(rootKids[0]), tmp.path() + QStringLiteral("/sub"));
    }

    void hardlinksCountedOnce()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString orig = tmp.filePath(QStringLiteral("orig.bin"));
        QVERIFY(writeFile(orig, 65536));
        QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("links")));
        const QString alias = tmp.filePath(QStringLiteral("links/alias.bin"));
        QCOMPARE(::link(QFile::encodeName(orig).constData(), QFile::encodeName(alias).constData()), 0);

        struct stat st {};
        QCOMPARE(::stat(QFile::encodeName(orig).constData(), &st), 0);
        const qint64 kernelPhysical = qint64(st.st_blocks) * 512;

        const ScanResult result = scanSync(tmp.path());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const FileGraph &g = result.graph;

        QCOMPARE(g.summary.fileCount, 2);
        QCOMPARE(g.summary.hardlinkedFileCount, 2);
        // Both links live inside the scanned tree: charged once at the root.
        QCOMPARE(g.summary.totalPhysical, kernelPhysical);
        // Per-node: each link individually frees 0.
        for (const NodeID kid : g.children(g.root())) {
            if (g.name(kid) == u"orig.bin")
                QCOMPARE(g.sizes(kid).unique, 0);
        }
    }

    void sparseFile()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("sparse.bin"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'y'));
        QVERIFY(f.seek(1 << 20)); // 1 MiB hole
        f.write(QByteArray(4096, 'y'));
        f.close();

        const ScanResult result = scanSync(tmp.path());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const FileGraph &g = result.graph;
        QCOMPARE(g.summary.sparseFileCount, 1);
        QCOMPARE(g.summary.totalLogical, qint64((1 << 20) + 4096));
        QVERIFY(g.summary.totalPhysical < g.summary.totalLogical);
    }

    void symlinkLoopTerminates()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("dir")));
        // dir/back -> .. creates a potential cycle for naive traversers.
        const QByteArray linkPath = QFile::encodeName(tmp.filePath(QStringLiteral("dir/back")));
        QCOMPARE(::symlink("..", linkPath.constData()), 0);

        const ScanResult result = scanSync(tmp.path());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        // Symlinks are recorded but never traversed; scan terminates.
        QCOMPARE(result.graph.summary.symlinkCount, 1);
    }

    void permissionDeniedRecorded()
    {
        if (::geteuid() == 0)
            QSKIP("root can read anything");
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("locked")));
        QVERIFY(writeFile(tmp.filePath(QStringLiteral("locked/secret.bin")), 1234));
        QCOMPARE(::chmod(QFile::encodeName(tmp.filePath(QStringLiteral("locked"))).constData(), 0), 0);

        const ScanResult result = scanSync(tmp.path());
        ::chmod(QFile::encodeName(tmp.filePath(QStringLiteral("locked"))).constData(), 0755);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.graph.summary.deniedDirectoryCount, 1);
        QCOMPARE(result.graph.ledger.deniedCount, 1);
    }

    void removingPatchesRollups()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeFile(tmp.filePath(QStringLiteral("a.bin")), 10000));
        QVERIFY(writeFile(tmp.filePath(QStringLiteral("b.bin")), 20000));

        const ScanResult result = scanSync(tmp.path());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const FileGraph &g = result.graph;
        QCOMPARE(g.summary.totalLogical, 30000);

        NodeID b;
        for (const NodeID kid : g.children(g.root()))
            if (g.name(kid) == u"b.bin")
                b = kid;
        QVERIFY(b.raw > 0);

        const FileGraph patched = g.removing(b);
        QCOMPARE(patched.summary.totalLogical, 10000);
        QCOMPARE(patched.childCount(patched.root()), 1);
        QCOMPARE(patched.name(patched.children(patched.root())[0]), QStringLiteral("a.bin"));
    }

    void notADirectory()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeFile(tmp.filePath(QStringLiteral("plain.bin")), 1));
        QString error;
        auto session = ScanEngine::scan(tmp.filePath(QStringLiteral("plain.bin")), {}, &error);
        QVERIFY(!session);
        QVERIFY(error.contains(QStringLiteral("Not a directory")));
    }
};

QTEST_GUILESS_MAIN(TestScan)
#include "test_scan.moc"
