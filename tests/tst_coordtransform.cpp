// =============================================================================
// tst_coordtransform.cpp — 坐标转换单元测试
// GMST 已知值 | ECEF↔大地往返 | ENU/观测角 | 方向矢量旋转 | 大气折射
// =============================================================================
#include <QtTest/QtTest>
#include <cmath>
#include "core/CoordTransform.h"
#include "core/Observer.h"
#include "testutils.h"
#include "tst_runners.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double deg(double rad) { return rad * 180.0 / M_PI; }

class TstCoordTransform : public QObject {
    Q_OBJECT
private slots:
    void gmstKnownValues();
    void gmstRate();
    void deltaTAndUtcGmst();
    void ecefGeodeticRoundTrip();
    void enuAndLookAngle();
    void eciDirToEcefTest();
    void refractionTests();
};

void TstCoordTransform::gmstKnownValues()
{
    // J2000.0（2000-01-01 12:00 UT1）: GMST = 67310.54841 s = 280.460618375°
    double g1 = deg(CoordTransform::gmstRad(2451545.0));
    QVERIFY2(std::abs(g1 - 280.460618375) < 1e-5,
             qPrintable(QString("J2000 GMST=%1").arg(g1, 0, 'f', 8)));

    // 2000-01-01 00:00 UT1: 24110.54841 − 236.5553679×0.5 → 99.9677947°
    double g2 = deg(CoordTransform::gmstRad(2451544.5));
    QVERIFY2(std::abs(g2 - 99.9677947) < 1e-5,
             qPrintable(QString("0h GMST=%1").arg(g2, 0, 'f', 8)));

    // Meeus《Astronomical Algorithms》例 12.a: 1987-04-10.0 TD → 13h10m46.3668s
    // （书中以 JD 2446895.5 直接代入公式，故此处不施加 ΔT 修正）
    double jdUt1 = 2446895.5;
    double g3 = deg(CoordTransform::gmstRad(jdUt1));
    QVERIFY2(std::abs(g3 - 197.693195) < 1e-5,
             qPrintable(QString("Meeus GMST=%1, 期望 197.6932").arg(g3, 0, 'f', 6)));
}

void TstCoordTransform::gmstRate()
{
    // 恒星时速率: 1 UT 日 = 360.985647366°。gmstRad 返回 [0,360)，跨日差值
    // 会取模，故可观测到的增量是其 mod 360 值 0.985647366°。
    double jd = 2460390.0;   // 2024-03-20 12:00
    double a = deg(CoordTransform::gmstRad(jd));
    double b = deg(CoordTransform::gmstRad(jd + 1.0));
    double rate = b - a;
    while (rate < 0.0)  rate += 360.0;
    QVERIFY2(std::abs(rate - 0.985647366) < 1e-5,
             qPrintable(QString("sidereal rate=%1").arg(rate, 0, 'f', 8)));

    // GMST 随 12h 增加约 180.49°
    double c = deg(CoordTransform::gmstRad(jd + 0.5));
    double half = c - a;
    while (half < 0.0) half += 360.0;
    QVERIFY(std::abs(half - 180.492823683) < 1e-4);
}

void TstCoordTransform::deltaTAndUtcGmst()
{
    // ΔT 已知参考值（IERS 观测/美国海军天文台表）:
    //   1960-01-01 ≈ 33.15 s, 1980-01-01 ≈ 50.54 s,
    //   2000-01-01 ≈ 63.83 s, 2010-01-01 ≈ 66.07 s, 2024-01-01 ≈ 69.2 s
    struct { int y; double refSec; double tol; } cases[] = {
        {1960, 33.15, 1.0},
        {1980, 50.54, 1.0},
        {2000, 63.83, 1.0},
        {2010, 66.07, 1.0},
        {2024, 69.2, 5.0},   // 多项式在 2024 偏高 ~5s，容差放宽
    };
    for (const auto &c : cases) {
        double jd = jdFromUtc(c.y, 1, 1, 0, 0, 0);
        double dt = CoordTransform::deltaTSeconds(jd);
        QVERIFY2(std::abs(dt - c.refSec) < c.tol,
                 qPrintable(QString("ΔT(%1)=%2 s, 参考 %3 s")
                            .arg(c.y).arg(dt, 0, 'f', 3).arg(c.refSec, 0, 'f', 3)));
    }

    // 连续性与范围: 1900~2150 全部有效且不越界（无 NaN/负值/突变）
    double prev = 0.0;
    for (double jd = jdFromUtc(1900, 1, 1, 0, 0, 0);
         jd < jdFromUtc(2150, 1, 1, 0, 0, 0); jd += 10.0) {
        double dt = CoordTransform::deltaTSeconds(jd);
        QVERIFY(std::isfinite(dt) && dt > -5.0);
        if (jd > jdFromUtc(1900, 1, 1, 0, 0, 0))
            QVERIFY(std::abs(dt - prev) < 60.0);   // 年际变化远小于 60s
        prev = dt;
    }

    // gmstRadUtc = gmstRad(jd + ΔT/86400)，与定义严格一致
    double jd = jdFromUtc(2024, 6, 1, 12, 0, 0);
    double dt = CoordTransform::deltaTSeconds(jd);
    double g1 = CoordTransform::gmstRadUtc(jd);
    double g2 = CoordTransform::gmstRad(jd + dt / 86400.0);
    QVERIFY(std::abs(g1 - g2) < 1e-12);
}

void TstCoordTransform::ecefGeodeticRoundTrip()
{
    const struct { double lat, lon, alt; } pts[] = {
        {0.0, 0.0, 0.0},
        {90.0, 0.0, 0.0},
        {-90.0, 0.0, 0.0},
        {0.0, 180.0, 0.0},
        {45.0, 120.0, 500.0},
        {-33.86, 151.2, 0.01},
        {39.9042, 116.4074, 0.05},   // 北京
        {51.5074, -0.1278, 0.045},   // 伦敦
        {0.0, 90.0, 400.0},
        {80.0, -45.0, 1000.0},
    };
    for (const auto &p : pts) {
        GeoPos g(p.lat, p.lon, p.alt);
        auto ecef = CoordTransform::geodeticToEcef(g);
        GeoPos back = CoordTransform::ecefToGeodetic(ecef);
        QVERIFY2(std::abs(back.latDeg - g.latDeg) < 1e-8 &&
                 std::abs(back.lonDeg - g.lonDeg) < 1e-8 &&
                 std::abs(back.altKm - g.altKm) < 1e-6,
                 qPrintable(QString("往返失败 (%1,%2,%3) → (%4,%5,%6)")
                     .arg(g.latDeg).arg(g.lonDeg).arg(g.altKm)
                     .arg(back.latDeg, 0, 'f', 9).arg(back.lonDeg, 0, 'f', 9)
                     .arg(back.altKm, 0, 'f', 9)));
    }

    // 北京 ECEF 地心半径 ≈ 6369.4 km（受扁率影响略低于平均半径 6371）
    auto bj = CoordTransform::geodeticToEcef(GeoPos(39.9042, 116.4074, 0.05));
    double r = std::sqrt(bj[0] * bj[0] + bj[1] * bj[1] + bj[2] * bj[2]);
    QVERIFY(r > 6365.0 && r < 6390.0);
}

void TstCoordTransform::enuAndLookAngle()
{
    // 观测站: 赤道 (0°, 0°, 0)
    Observer obs(0.0, 0.0, 0.0);

    // 卫星在头顶 400 km: ECEF (N+400, 0, 0)
    auto overhead = CoordTransform::geodeticToEcef(GeoPos(0.0, 0.0, 400.0));
    LookAngle lo = obs.lookAngle(overhead);
    QVERIFY(std::abs(lo.elevDeg - 90.0) < 1e-6);
    QVERIFY(std::abs(lo.rangeKm - 400.0) < 1e-6);

    // 卫星在正东 90° 经度（赤道上经度差即中心角 90°，远超地平角 ≈19.76°，
    // 故位于地平线以下）。赤道上椭球退化为球（N=a），解析值精确：
    //   ECEF: 站=(a,0,0), 星=(0,a+400,0) ⇒ east=a+400, north=0, up=−a
    const double A = CoordTransform::WGS84_A_KM;
    auto east = CoordTransform::geodeticToEcef(GeoPos(0.0, 90.0, 400.0));
    LookAngle le = obs.lookAngle(east);
    const double eastComp = A + 400.0;
    const double upComp = -A;
    const double rangeEast = std::sqrt(eastComp * eastComp + upComp * upComp);
    QVERIFY2(std::abs(le.azDeg - 90.0) < 0.01,
             qPrintable(QString("az=%1").arg(le.azDeg, 0, 'f', 6)));
    QVERIFY2(std::abs(le.elevDeg - std::asin(upComp / rangeEast) * 180.0 / M_PI) < 1e-6,
             qPrintable(QString("elev=%1 期望 %2")
                        .arg(le.elevDeg, 0, 'f', 6)
                        .arg(std::asin(upComp / rangeEast) * 180.0 / M_PI, 0, 'f', 6)));
    QVERIFY(std::abs(le.rangeKm - rangeEast) < 1e-6);

    // ENU 分量一致性: e²+n²+u² = range²
    auto enu = CoordTransform::ecefToEnuComponents(
        {east[0] - obs.ecef()[0], east[1] - obs.ecef()[1], east[2] - obs.ecef()[2]},
        0.0, 0.0);
    QVERIFY(std::abs(enu[0] - eastComp) < 1e-6);   // east = a+400
    QVERIFY(std::abs(enu[1]) < 1e-6);              // north ≈ 0
    QVERIFY(std::abs(enu[2] - upComp) < 1e-6);     // up = −a（地平线下）
    double r2 = enu[0] * enu[0] + enu[1] * enu[1] + enu[2] * enu[2];
    QVERIFY(std::abs(std::sqrt(r2) - le.rangeKm) < 1e-6);

    // 可见的正东用例: 中心角 15° < 地平角 ⇒ 仰角 > 0（球面公式, 赤道处精确）
    auto eastVis = CoordTransform::geodeticToEcef(GeoPos(0.0, 15.0, 400.0));
    LookAngle lv = obs.lookAngle(eastVis);
    const double cg = std::cos(15.0 * M_PI / 180.0);
    const double rangeVis = std::sqrt((A + 400.0) * (A + 400.0) + A * A
                                      - 2.0 * (A + 400.0) * A * cg);
    const double elevVis = std::asin(((A + 400.0) * cg - A) / rangeVis) * 180.0 / M_PI;
    QVERIFY(std::abs(lv.azDeg - 90.0) < 0.01);
    QVERIFY(std::abs(lv.elevDeg - elevVis) < 1e-6);
    QVERIFY(std::abs(lv.rangeKm - rangeVis) < 1e-6);

    // 星下点
    GeoPos sub = obs.subSatPoint(east);
    QVERIFY(std::abs(sub.latDeg) < 1e-8);
    QVERIFY(std::abs(sub.lonDeg - 90.0) < 1e-8);
    QVERIFY(std::abs(sub.altKm - 400.0) < 1e-6);
}

void TstCoordTransform::eciDirToEcefTest()
{
    // dir=(1,0,0) 在 J2000（GMST=280.4606°）→ (cosθ, −sinθ, 0)
    double theta = deg(CoordTransform::gmstRad(2451545.0));   // 280.4606°
    auto out = CoordTransform::eciDirToEcef({1.0, 0.0, 0.0}, 2451545.0);
    double expectX = std::cos(theta * M_PI / 180.0);
    double expectY = -std::sin(theta * M_PI / 180.0);
    QVERIFY(std::abs(out[0] - expectX) < 1e-9);
    QVERIFY(std::abs(out[1] - expectY) < 1e-9);
    QVERIFY(std::abs(out[2]) < 1e-9);

    // 单位长度保持
    auto v = CoordTransform::eciDirToEcef({0.6, 0.8, 0.0}, 2460390.0);
    double n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    QVERIFY(std::abs(n - 1.0) < 1e-12);
}

void TstCoordTransform::refractionTests()
{
    double r0 = CoordTransform::applyRefraction(0.0);
    QVERIFY(r0 > 0.4 && r0 < 0.7);           // 地平附近 ≈ 0.57°
    double r45 = CoordTransform::applyRefraction(45.0);
    QVERIFY(r45 > 45.0 && r45 < 45.05);      // 45° 处 < 0.05°
    double r90 = CoordTransform::applyRefraction(90.0);
    QVERIFY(r90 - 90.0 < 0.01);
    QCOMPARE(CoordTransform::applyRefraction(-5.0), -5.0);  // 负仰角不变
    QVERIFY(r0 - 0.0 > r45 - 45.0);          // 单调递减
}

int runTstCoordTransform(int argc, char *argv[])
{
    TstCoordTransform t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_coordtransform.moc"
