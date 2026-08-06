// =============================================================================
// tst_tleblocks.cpp — 多 TLE 文本块解析单元测试
// 覆盖: 标准 3LE | 纯两行 | 名称行误判防护 | 注释行 | 短行/畸形输入
// =============================================================================
#include <QtTest/QtTest>
#include "business/TleBlocks.h"
#include "tst_runners.h"

class TstTleBlocks : public QObject {
    Q_OBJECT
private slots:
    void singleThreeLine();
    void multipleBlocks();
    void twoLineOnly();
    void nameLikeLinesAreNotTle();
    void shortLinesIgnored();
    void emptyAndGarbageInput();
};

namespace {

// 标准 69 字符 TLE 行
const QString L1 = QStringLiteral(
    "1 25544U 98067A   24083.56167013  .00012345  00000-0  24655-3 0  9994");
const QString L2 = QStringLiteral(
    "2 25544  51.6423  23.4567 0004567 123.4567 236.7890 15.50012345678901");

} // namespace

void TstTleBlocks::singleThreeLine()
{
    const QString text = "ISS (ZARYA)\n" + L1 + "\n" + L2 + "\n";
    const auto blocks = parseTleBlocks(text);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].name, QString("ISS (ZARYA)"));
    QCOMPARE(blocks[0].line1, L1);
    QCOMPARE(blocks[0].line2, L2);
}

void TstTleBlocks::multipleBlocks()
{
    const QString text = "SAT-A\n" + L1 + "\n" + L2 + "\n"
                       + "SAT-B\n" + L1 + "\n" + L2 + "\n";
    const auto blocks = parseTleBlocks(text);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].name, QString("SAT-A"));
    QCOMPARE(blocks[1].name, QString("SAT-B"));
}

void TstTleBlocks::twoLineOnly()
{
    const auto blocks = parseTleBlocks(L1 + "\n" + L2 + "\n");
    QCOMPARE(blocks.size(), 1);
    QVERIFY(blocks[0].name.isEmpty());
}

void TstTleBlocks::nameLikeLinesAreNotTle()
{
    // 以 "1" 开头的名称行/标题不得被误判为 TLE 行 1
    const QString text = "1U CUBESAT-X\n" + L1 + "\n" + L2 + "\n"
                       + "1999-2024 项目资料\n";
    const auto blocks = parseTleBlocks(text);
    QCOMPARE(blocks.size(), 1);
    // 名称行最终被真正 TLE 前的最后一条非 TLE 行覆盖
    QCOMPARE(blocks[0].name, QString("1U CUBESAT-X"));
}

void TstTleBlocks::shortLinesIgnored()
{
    // 长度不足 69 的 "1 xxx" 行不能作为 TLE 行（行 2 到来时无行 1 → 丢弃）
    const QString text = "1 bad short line\n2 bad short line\n" + L1 + "\n" + L2 + "\n";
    const auto blocks = parseTleBlocks(text);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].line1, L1);
}

void TstTleBlocks::emptyAndGarbageInput()
{
    QVERIFY(parseTleBlocks(QString()).isEmpty());
    QVERIFY(parseTleBlocks(QString("\n\n   \n")).isEmpty());
    QVERIFY(parseTleBlocks(QString("随便一段文字\n没有 TLE\n")).isEmpty());
}

int runTstTleBlocks(int argc, char *argv[])
{
    TstTleBlocks t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_tleblocks.moc"
