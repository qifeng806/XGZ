// =============================================================================
// tst_visualmagnitude.cpp — 星等估算单元测试
// 覆盖: 固有星等目录匹配 | 相位角几何 | Vallerie 1963 公式数值
// =============================================================================
#include <QtTest/QtTest>
#include "core/VisualMagnitude.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class TstVisualMagnitude : public QObject {
    Q_OBJECT
private slots:
    void standardMagnitudeLookup();
    void phaseAngleExtremes();
    void visualMagnitudeFormula();
};

void TstVisualMagnitude::standardMagnitudeLookup()
{
    // 目录匹配（大小写: 卫星名来自 TLE 通常大写）
    QCOMPARE(VisualMagnitudeCalc::standardMagnitude("ISS (ZARYA)"), -1.8);
    QCOMPARE(VisualMagnitudeCalc::standardMagnitude("TIANGONG 3"), -0.9);
    // 未命中 → 回退值
    QCOMPARE(VisualMagnitudeCalc::standardMagnitude("FUNKY-CUBE"), 4.5);
    QCOMPARE(VisualMagnitudeCalc::standardMagnitude("FUNKY-CUBE", 6.0), 6.0);
    // 空名称 → 回退值
    QCOMPARE(VisualMagnitudeCalc::standardMagnitude(""), 4.5);
}

void TstVisualMagnitude::phaseAngleExtremes()
{
    std::array<double, 3> sat{0, 0, 1000};
    // 满相位 B≈0: 观测者在卫星与太阳之间（观测者看受照面）
    std::array<double, 3> sun{0, 0, 1500};
    std::array<double, 3> obs{0, 0, 2000};
    QVERIFY(std::abs(VisualMagnitudeCalc::phaseAngleRad(sat, sun, obs)) < 1e-9);

    // 背日 B≈π: 太阳在卫星另一侧（观测者看暗面）
    obs = {0, 0, 0};
    QVERIFY(std::abs(VisualMagnitudeCalc::phaseAngleRad(sat, sun, obs) - M_PI) < 1e-9);

    // 侧向 B≈π/4: 观测方向 (1/√2, 0, 1/√2) 与太阳方向 (0,0,1) 夹角 45°
    obs = {1000, 0, 2000};
    double b = VisualMagnitudeCalc::phaseAngleRad(sat, sun, obs);
    double cosB = std::cos(b);
    QVERIFY(std::abs(cosB - 1.0 / std::sqrt(2.0)) < 1e-9);

    // 退化几何（重合点）→ 按 π 处理，不得 NaN
    std::array<double, 3> bad{0, 0, 1000};
    QVERIFY(std::isfinite(VisualMagnitudeCalc::phaseAngleRad(bad, bad, obs)));
}

void TstVisualMagnitude::visualMagnitudeFormula()
{
    // 满相位 1000 km → 视星等 = 固有星等（公式基准）
    QVERIFY(std::abs(VisualMagnitudeCalc::visualMagnitude(4.5, 1000.0, 0.0) - 4.5) < 1e-9);

    // 距离平方率: 2000 km 比 1000 km 暗 5·log10(2) ≈ 1.505 星等
    const double m1 = VisualMagnitudeCalc::visualMagnitude(4.5, 1000.0, 0.0);
    const double m2 = VisualMagnitudeCalc::visualMagnitude(4.5, 2000.0, 0.0);
    QVERIFY(std::abs((m2 - m1) - 5.0 * std::log10(2.0)) < 1e-9);

    // 相位亏缺: 侧向（B=π/2, f=1/π）比满相位暗 ~1.24 星等
    const double mSide = VisualMagnitudeCalc::visualMagnitude(4.5, 1000.0, M_PI / 2.0);
    QVERIFY(std::abs((mSide - m1) + 2.5 * std::log10(1.0 / M_PI)) < 1e-9);

    // 背日钳制: 不得发散为 ∞/NaN
    const double mDark = VisualMagnitudeCalc::visualMagnitude(4.5, 1000.0, M_PI);
    QVERIFY(std::isfinite(mDark));
    QVERIFY(mDark > 10.0);   // 极暗但有限

    // 单调性: 距离增大变暗、相位增大变暗
    QVERIFY(VisualMagnitudeCalc::visualMagnitude(4.5, 3000.0, 0.0) > m1);
    QVERIFY(VisualMagnitudeCalc::visualMagnitude(4.5, 1000.0, 1.0) > m1);
}

int runTstVisualMagnitude(int argc, char *argv[])
{
    TstVisualMagnitude t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_visualmagnitude.moc"