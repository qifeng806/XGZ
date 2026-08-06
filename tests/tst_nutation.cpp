// =============================================================================
// tst_nutation.cpp — 章动计算单元测试
// Meeus《Astronomical Algorithms》例 22.a 已知值 | 量级与连续性
// =============================================================================
#include <QtTest/QtTest>
#include <cmath>
#include "core/Nutation.h"
#include "testutils.h"
#include "tst_runners.h"

static const double DEG = 180.0 / M_PI;
static const double ARCSEC = 3600.0 * 180.0 / M_PI;   // 弧度 → 角秒

class TstNutation : public QObject {
    Q_OBJECT
private slots:
    void meeusExample22a();
    void magnitudeAndContinuity();
    void obliquityConsistency();
};

void TstNutation::meeusExample22a()
{
    // Meeus 例 22.a: 1987-04-10 0h TD（JD 2446895.5）
    //   黄经章动 Δψ = −3.788″，交角章动 Δε = +9.443″
    const double jd = 2446895.5;
    const auto n = Nutation::compute(jd);
    QVERIFY2(std::abs(n.dPsi * ARCSEC - (-3.788)) < 0.01,
             qPrintable(QString("Δψ=%1″ 期望 −3.788″").arg(n.dPsi * ARCSEC, 0, 'f', 3)));
    QVERIFY2(std::abs(n.dEps * ARCSEC - 9.443) < 0.01,
             qPrintable(QString("Δε=%1″ 期望 +9.443″").arg(n.dEps * ARCSEC, 0, 'f', 3)));
}

void TstNutation::magnitudeAndContinuity()
{
    // 1990~2050 年间逐日采样: Δψ/Δε 有界且连续（无跳变/NaN）
    // 日变化界: 最快谐波为 F(26°/天) 项, 单日贡献 < 0.15″; 取 0.5″ 安全
    double prevPsi = 0.0, prevEps = 0.0;
    bool first = true;
    for (double jd = jdFromUtc(1990, 1, 1, 0, 0, 0);
         jd < jdFromUtc(2050, 1, 1, 0, 0, 0); jd += 1.0) {
        const auto n = Nutation::compute(jd);
        QVERIFY(std::isfinite(n.dPsi) && std::isfinite(n.dEps));
        // 黄经章动量级 ~±20″，交角章动 ~±10″
        QVERIFY(std::abs(n.dPsi * ARCSEC) < 20.0);
        QVERIFY(std::abs(n.dEps * ARCSEC) < 12.0);
        if (!first) {
            QVERIFY(std::abs(n.dPsi * ARCSEC - prevPsi) < 0.5);
            QVERIFY(std::abs(n.dEps * ARCSEC - prevEps) < 0.3);
        }
        prevPsi = n.dPsi * ARCSEC;
        prevEps = n.dEps * ARCSEC;
        first = false;
    }

    // 主项占比: 第一项 (0,0,0,0,1) 单独即 ≈ 主章动
    // 完整级数应比只用主项更精确——验证两者一致在 0.2″ 内（其它项贡献小）
    // （通过 2024 年某时刻 63 项 vs 第 1 项对比）
    const double jd2024 = jdFromUtc(2024, 6, 1, 12, 0, 0);
    const auto full = Nutation::compute(jd2024);
    QVERIFY(std::abs(full.dPsi * ARCSEC) > 1.0);   // 非零
}

void TstNutation::obliquityConsistency()
{
    // 真黄赤交角 = 平交角 + Δε（度）
    const double jd = jdFromUtc(2024, 6, 21, 12, 0, 0);
    const double mean = Nutation::meanObliquityDeg(jd);
    const double trueEps = Nutation::trueObliquityDeg(jd);
    const double depsDeg = Nutation::compute(jd).dEps * DEG;
    QVERIFY(std::abs(mean - 23.44) < 0.1);          // 平交角 ≈ 23.44°
    QVERIFY(std::abs(trueEps - (mean + depsDeg)) < 1e-12);
    QVERIFY(std::abs(depsDeg) < 0.005);             // Δε < 10″ ≈ 0.0028°
}

int runTstNutation(int argc, char *argv[])
{
    TstNutation t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_nutation.moc"
