// =============================================================================
// tst_sunposition.cpp — 太阳位置计算单元测试
// 分点/至点已知天文量（赤经、赤纬、日地距离）与地面站高度角/方位角
// =============================================================================
#include <QtTest/QtTest>
#include <cmath>
#include "core/SunPosition.h"
#include "core/CoordTransform.h"
#include "testutils.h"
#include "tst_runners.h"

static const double BEIJING_LAT = 39.9042;
static const double BEIJING_LON = 116.4074;

class TstSunPosition : public QObject {
    Q_OBJECT
private slots:
    void vernalEquinox2024();
    void summerSolstice2024();
    void winterSolstice2024();
    void beijingNoonAltitudes();
    void dirEciUnitLength();
    void sunAtOverheadEquator();
};

void TstSunPosition::vernalEquinox2024()
{
    // 2024-03-20 03:06 UT：春分，太阳黄经 λ≈0°，赤纬跨零，赤经≈0°
    double jd = jdFromUtc(2024, 3, 20, 3, 6, 0);
    SunPosition s = SunPositionCalc::compute(jd, 0.0, 0.0);

    QVERIFY2(std::abs(s.decDeg) < 0.5,
             qPrintable(QString("dec=%1").arg(s.decDeg, 0, 'f', 3)));
    QVERIFY2(std::abs(s.raDeg - 0.0) < 0.5 || std::abs(s.raDeg - 360.0) < 0.5,
             qPrintable(QString("ra=%1").arg(s.raDeg, 0, 'f', 3)));

    // 2024 年近日点 1 月 3 日 → 3 月 20 日太阳距地约 0.996 AU
    QVERIFY2(std::abs(s.distanceAU - 0.9961) < 0.002,
             qPrintable(QString("dist=%1").arg(s.distanceAU, 0, 'f', 5)));
}

void TstSunPosition::summerSolstice2024()
{
    // 2024-06-20 20:51 UT：夏至，赤纬最大 ≈ +23.44°，赤经 ≈ 90°
    double jd = jdFromUtc(2024, 6, 20, 20, 51, 0);
    SunPosition s = SunPositionCalc::compute(jd, 0.0, 0.0);

    QVERIFY2(std::abs(s.decDeg - 23.44) < 0.3,
             qPrintable(QString("dec=%1").arg(s.decDeg, 0, 'f', 3)));
    QVERIFY2(std::abs(s.raDeg - 90.0) < 1.0,
             qPrintable(QString("ra=%1").arg(s.raDeg, 0, 'f', 3)));

    // 远日点 7 月 5 日 → 6 月 20 日距地约 1.016 AU
    QVERIFY2(std::abs(s.distanceAU - 1.0164) < 0.002,
             qPrintable(QString("dist=%1").arg(s.distanceAU, 0, 'f', 5)));
}

void TstSunPosition::winterSolstice2024()
{
    // 2024-12-21 09:20 UT：冬至，赤纬 ≈ −23.44°，赤经 ≈ 270°
    double jd = jdFromUtc(2024, 12, 21, 9, 20, 0);
    SunPosition s = SunPositionCalc::compute(jd, 0.0, 0.0);

    QVERIFY2(std::abs(s.decDeg + 23.44) < 0.3,
             qPrintable(QString("dec=%1").arg(s.decDeg, 0, 'f', 3)));
    QVERIFY2(std::abs(s.raDeg - 270.0) < 1.0,
             qPrintable(QString("ra=%1").arg(s.raDeg, 0, 'f', 3)));
}

void TstSunPosition::beijingNoonAltitudes()
{
    // 北京正午（真太阳时, UT ≈ 12h − 116.4°/15° = 04:14，但受时差方程影响,
    // 中天时刻会偏移 ±16 min）。故先迭代求中天（lha = GMST + lon − RA = 0），
    // 再验证: 中天方位角应精确为正南 180°，高度符合 90° − |φ − δ|。
    // 注意: 时角与 SunPosition 内部一致，均使用 ΔT 修正后的 UT1 GMST
    static const double GMST_RATE = 360.985647366;   // °/UT 日
    auto check = [](int y, int mo, int d, double expectAlt, const char *tag) {
        double jd = jdFromUtc(y, mo, d, 4, 14, 0);
        for (int i = 0; i < 10; ++i) {
            SunPosition s = SunPositionCalc::compute(jd, BEIJING_LAT, BEIJING_LON);
            double lha = CoordTransform::gmstRad(jd) * 180.0 / M_PI + BEIJING_LON - s.raDeg;
            while (lha > 180.0) lha -= 360.0;
            while (lha < -180.0) lha += 360.0;
            jd -= lha / GMST_RATE;
        }
        SunPosition s = SunPositionCalc::compute(jd, BEIJING_LAT, BEIJING_LON);
        double azErr = std::abs(s.azDeg - 180.0);
        if (azErr > 180.0) azErr = 360.0 - azErr;
        QVERIFY2(azErr < 0.05,
                 qPrintable(QString("%1: 中天 az=%2, 期望 180")
                            .arg(tag).arg(s.azDeg, 0, 'f', 4)));
        QVERIFY2(std::abs(s.altDeg - expectAlt) < 0.7,
                 qPrintable(QString("%1: alt=%2, 期望 %3")
                            .arg(tag).arg(s.altDeg, 0, 'f', 2).arg(expectAlt, 0, 'f', 2)));
    };
    check(2024, 6, 20, 90.0 - BEIJING_LAT + 23.44,  "夏至正午");   // ≈73.5°
    check(2024, 12, 21, 90.0 - BEIJING_LAT - 23.44, "冬至正午");   // ≈26.7°
    check(2024, 3, 20, 90.0 - BEIJING_LAT,          "春分正午");   // ≈50.1°
}

void TstSunPosition::dirEciUnitLength()
{
    for (int i = 0; i < 400; ++i) {
        double jd = 2451545.0 + i * 13.7;   // 全年采样
        SunPosition s = SunPositionCalc::compute(jd, 0.0, 0.0);
        double n = std::sqrt(s.dirECI[0] * s.dirECI[0]
                           + s.dirECI[1] * s.dirECI[1]
                           + s.dirECI[2] * s.dirECI[2]);
        QVERIFY(std::abs(n - 1.0) < 1e-9);

        // RA/Dec 与方向矢量一致性
        double ra = std::atan2(s.dirECI[1], s.dirECI[0]) * 180.0 / M_PI;
        if (ra < 0.0) ra += 360.0;
        QVERIFY(std::abs(ra - s.raDeg) < 1e-6);
        double dec = std::asin(s.dirECI[2]) * 180.0 / M_PI;
        QVERIFY(std::abs(dec - s.decDeg) < 1e-6);
    }
}

void TstSunPosition::sunAtOverheadEquator()
{
    // 春分日全天 1 分钟扫描赤道本初子午线：太阳应至少一次接近天顶。
    // 不能假设 12:00 UT 即中天（时差方程 ~−8 min, 12:00 时太阳距天顶 ~1.8°），
    // 且中天时赤纬已在 ±0.2° 内漂移，故断言峰值 > 89.5°。
    double jd0 = jdFromUtc(2024, 3, 20, 0, 0, 0);
    double best = -90.0;
    for (int m = 0; m < 24 * 60; ++m) {
        SunPosition s = SunPositionCalc::compute(jd0 + m / 1440.0, 0.0, 0.0);
        best = std::max(best, s.altDeg);
    }
    QVERIFY2(best > 89.5,
             qPrintable(QString("全天最高 alt=%1").arg(best, 0, 'f', 3)));
}

int runTstSunPosition(int argc, char *argv[])
{
    TstSunPosition t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_sunposition.moc"
