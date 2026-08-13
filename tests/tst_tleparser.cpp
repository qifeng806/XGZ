// =============================================================================
// tst_tleparser.cpp — TLE 解析器单元测试
// =============================================================================
#include <QtTest/QtTest>
#include <cmath>
#include "core/TleParser.h"
#include "tst_runners.h"

// 参考 TLE（与 perturb 官方测试同源，历元 2022-03-12 18:43:40）
static const char *ISS_L1 =
    "1 25544U 98067A   22071.78032407  .00021395  00000-0  39008-3 0  9996";
static const char *ISS_L2 =
    "2 25544  51.6424  94.0370 0004047 256.5103  89.8846 15.49386383330227";

class TstTleParser : public QObject {
    Q_OBJECT
private slots:
    void parseBasicFields();
    void parseScientificData();
    void checksumTests();
    void errorPaths();
    void classifyModelTests();
    void epochAndPeriod();
};

void TstTleParser::parseBasicFields()
{
    TleParser::ParseError err;
    auto tle = TleParser::parse("ISS (ZARYA)", ISS_L1, ISS_L2, &err);
    QVERIFY(tle.has_value());
    QCOMPARE(err.code, TleParser::ParseError::OK);

    QCOMPARE(QString::fromStdString(tle->name), "ISS (ZARYA)");
    QCOMPARE(tle->noradID, 25544);
    QCOMPARE(tle->epochYear, 2022);
    QVERIFY(std::abs(tle->epochDay - 71.78032407) < 1e-8);
    QVERIFY(std::abs(tle->bstar - 3.9008e-4) < 1e-12);
    QVERIFY(std::abs(tle->inclinationDeg - 51.6424) < 1e-8);
    QVERIFY(std::abs(tle->raanDeg - 94.0370) < 1e-8);
    QVERIFY(std::abs(tle->eccentricity - 0.0004047) < 1e-10);
    QVERIFY(std::abs(tle->argPerigeeDeg - 256.5103) < 1e-8);
    QVERIFY(std::abs(tle->meanAnomalyDeg - 89.8846) < 1e-8);
    QVERIFY(std::abs(tle->meanMotionRevDay - 15.49386383) < 1e-8);
    QCOMPARE(tle->elsetNum, 999);
    QCOMPARE(tle->revNumber, 33022);
    QVERIFY(tle->checksumValid);
    QCOMPARE(tle->ephemerisType, Sgp4Type::LEO_SGP4);

    // 无名称行：默认 NORAD 名称
    auto tle2 = TleParser::parse(ISS_L1, ISS_L2, &err);
    QVERIFY(tle2.has_value());
    QCOMPARE(QString::fromStdString(tle2->name), "NORAD 25544");
}

void TstTleParser::parseScientificData()
{
    struct Case { const char *s; double expect; };
    const Case cases[] = {
        // 来源: SGP4-VER.TLE 中真实 B* 字段
        {" 28098-4", 0.28098e-4},   // 2.8098e-5
        {" 10000-3", 0.10000e-3},   // 1.0e-4
        {" 12808-3", 0.12808e-3},
        {" 11873-3", 0.11873e-3},
        {" 14311-1", 0.14311e-1},   // 1.4311e-3
        {" 18784-3", 0.18784e-3},
        {" 00000+0", 0.0},
        {" 00000-0", 0.0},
        {"-13525-3", -0.13525e-3},  // -1.3525e-4
        {" 49949-3", 0.49949e-3},   // 4.9949e-4
        {" 29868-3", 0.29868e-3},
        {" 76590-3", 0.76590e-3},
        {" 12956-2", 0.12956e-2},   // 1.2956e-3
        {" 24655-3", 0.24655e-3},   // UI 占位符示例
        {" 39008-3", 0.39008e-3},
    };
    for (const auto &c : cases) {
        double got = TleParser::parseScientific(c.s);
        QVERIFY2(std::abs(got - c.expect) < 1e-15,
                 qPrintable(QString("%1 -> %2, 期望 %3")
                            .arg(c.s).arg(got, 0, 'g', 16).arg(c.expect, 0, 'g', 16)));
    }
}

void TstTleParser::checksumTests()
{
    QVERIFY(TleParser::verifyChecksum(ISS_L1));
    QVERIFY(TleParser::verifyChecksum(ISS_L2));

    // 篡改一位数字 → 校验失败
    std::string bad1 = ISS_L1;
    bad1[20] = (bad1[20] == '0') ? '1' : '0';
    QVERIFY(!TleParser::verifyChecksum(bad1));

    // 短行 → 校验失败（而不是崩溃）
    QVERIFY(!TleParser::verifyChecksum("1 25544"));
}

void TstTleParser::errorPaths()
{
    TleParser::ParseError err;

    // 长度不足
    auto r1 = TleParser::parse("short", ISS_L2, &err);
    QVERIFY(!r1.has_value());
    QCOMPARE(err.code, TleParser::ParseError::BAD_LENGTH);

    // 行号错误
    std::string badLine1 = ISS_L1;
    badLine1[0] = 'X';
    auto r2 = TleParser::parse(badLine1, ISS_L2, &err);
    QVERIFY(!r2.has_value());
    QCOMPARE(err.code, TleParser::ParseError::BAD_LINE_NUMBER);

    // NORAD 非法
    std::string badNorad = ISS_L1;
    badNorad.replace(2, 5, "ABCDE");
    auto r3 = TleParser::parse(badNorad, ISS_L2, &err);
    QVERIFY(!r3.has_value());
    QCOMPARE(err.code, TleParser::ParseError::BAD_NORAD);

    // 历元非法（保持行长为 69: 替换串必须恰好 14 字符且含非数字）
    std::string badEpoch = ISS_L1;
    badEpoch.replace(18, 14, "XXXXXXXXXXXXXX");
    auto r4 = TleParser::parse(badEpoch, ISS_L2, &err);
    QVERIFY(!r4.has_value());
    QCOMPARE(err.code, TleParser::ParseError::BAD_EPOCH);

    // 倾角越界（范围检查）
    std::string badIncl = ISS_L2;
    badIncl.replace(8, 8, " 200.0000");
    auto r5 = TleParser::parse(ISS_L1, badIncl, &err);
    QVERIFY(!r5.has_value());
    QCOMPARE(err.code, TleParser::ParseError::BAD_ELEMENTS);

    // 校验和不一致 → 仍返回结果但标记
    std::string badCs = ISS_L1;
    badCs[68] = '0';
    auto r6 = TleParser::parse(badCs, ISS_L2, &err);
    QVERIFY(r6.has_value());
    QVERIFY(!r6->checksumValid);
    QCOMPARE(err.code, TleParser::ParseError::CHECKSUM_FAILED);

    // B* 指数超界（>9）→ 解析异常路径
    std::string badBstar = ISS_L1;
    badBstar.replace(53, 8, "12345-10");
    auto r7 = TleParser::parse(badBstar, ISS_L2, &err);
    QVERIFY(!r7.has_value());
    QCOMPARE(err.code, TleParser::ParseError::BAD_BS);

    // 行尾带 \r（Windows 文件直传）: 校验和被回车挤掉 → 应剥离后正常解析
    std::string crL1 = ISS_L1; crL1 += '\r';
    std::string crL2 = ISS_L2; crL2 += '\r';
    auto r8 = TleParser::parse(crL1, crL2, &err);
    QVERIFY(r8.has_value());
    QVERIFY(r8->checksumValid);

    // 非闰年历元日 366.5（上限 366, 日计数 1 起）→ 拒绝
    std::string badDay = ISS_L1;
    badDay.replace(18, 14, "25366.50000000");
    auto r9 = TleParser::parse(badDay, ISS_L2, &err);
    QVERIFY(!r9.has_value());
    QCOMPARE(err.code, TleParser::ParseError::BAD_EPOCH);

    // 闰年历元日 366.5（2024 闰年）→ 合法
    std::string leapDay = ISS_L1;
    leapDay.replace(18, 14, "24366.50000000");
    auto r10 = TleParser::parse(leapDay, ISS_L2, &err);
    QVERIFY(r10.has_value());
    QCOMPARE(r10->epochYear, 2024);
}

void TstTleParser::classifyModelTests()
{
    // 判据: 周期 ≥ 225 min（平运动 ≤ 6.4 rev/day）→ SDP4，与 Vallado/perturb 一致;
    // 深空带内周期 ≥ 1400 min（平运动 ≤ 1440/1400 ≈ 1.0286）→ GEO 同步带
    QCOMPARE(TleParser::classifyModel(15.49), Sgp4Type::LEO_SGP4);   // ISS
    QCOMPARE(TleParser::classifyModel(6.5),  Sgp4Type::LEO_SGP4);    // 略高于阈值
    QCOMPARE(TleParser::classifyModel(6.4),  Sgp4Type::MEO_SDP4);    // 225min 阈值点
    QCOMPARE(TleParser::classifyModel(2.0),  Sgp4Type::MEO_SDP4);    // MOLNIYA
    QCOMPARE(TleParser::classifyModel(1.05), Sgp4Type::MEO_SDP4);    // 周期 ≈1371min 仍属中轨
    QCOMPARE(TleParser::classifyModel(1.0),  Sgp4Type::GEO_SDP4_DS); // 同步带平运动 ≈1.0
    QCOMPARE(TleParser::classifyModel(0.99), Sgp4Type::GEO_SDP4_DS); // GEO
    QCOMPARE(TleParser::classifyModel(0.073),Sgp4Type::GEO_SDP4_DS); // WIND
}

void TstTleParser::epochAndPeriod()
{
    auto tle = TleParser::parse(ISS_L1, ISS_L2);
    QVERIFY(tle.has_value());

    // 周期 = 1440 / 平运动
    QVERIFY(std::abs(tle->periodMinutes() - 1440.0 / 15.49386383) < 1e-6);

    // 1957-10-04（Sputnik）→ 世纪判据（行需满 69 字符, 校验和已验证）
    auto tle57 = TleParser::parse(
        "1 00001U 57001A   57080.00000000  .00000000  00000-0  00000-0 0    05",
        "2 00001  65.0000   0.0000 0000000   0.0000   0.0000 15.00000000    00");
    QVERIFY(tle57.has_value());
    QCOMPARE(tle57->epochYear, 1957);
}

int runTstTleParser(int argc, char *argv[])
{
    TstTleParser t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_tleparser.moc"
