// =============================================================================
// tst_doppler.cpp — 多普勒频移单元测试
// 验证: 靠近为正/远离为负的符号约定、|f| = v_r/c·f 数值、
//       ECEF 静止(同步轨道)卫星零频移（哥里奥利速度项符号的关键验证）
// 自洽构造: 由期望的 ECEF 状态经 GMST 逆旋转得到 TEME 输入,
//           与引擎内部正向变换使用同一 gmstRad() 函数, 无独立时角假设
// =============================================================================
#include <QtTest/QtTest>
#include <cmath>
#include <array>
#include <utility>
#include "core/DopplerCalc.h"
#include "core/CoordTransform.h"
#include "tst_runners.h"

namespace {

constexpr double C_KM_S    = 299792.458;
constexpr double OMEGA_E   = 7.2921158553e-5;   // rad/s 地球自转角速度
constexpr double JD        = 2460390.0;         // 2024-03-20 12:00 UT

// ECEF 状态 → TEME 状态（逆向自洽变换）
// 引擎: ECEF = Rz(−θ)·TEME → TEME = Rz(+θ)·ECEF
std::pair<std::array<double, 3>, std::array<double, 3>>
ecefToTeme(const std::array<double, 3> &rE,
           const std::array<double, 3> &vE, double jd)
{
    const double th = CoordTransform::gmstRad(jd);
    const double c = std::cos(th), s = std::sin(th);
    auto rzPlus = [c, s](const std::array<double, 3> &v) -> std::array<double, 3> {
        return { c * v[0] - s * v[1],
                 s * v[0] + c * v[1],
                 v[2] };
    };
    // vECEF = Rz(−θ)·vTeme − ω×rECEF → vTeme = Rz(+θ)·(vECEF + ω×rECEF)
    const std::array<double, 3> omegaCrossR = { -OMEGA_E * rE[1], OMEGA_E * rE[0], 0.0 };
    const std::array<double, 3> vTarg = { vE[0] + omegaCrossR[0],
                                          vE[1] + omegaCrossR[1],
                                          vE[2] };
    return { rzPlus(rE), rzPlus(vTarg) };
}

} // namespace

class TstDoppler : public QObject {
    Q_OBJECT
private slots:
    void signAndMagnitude();
    void geostationaryZeroShift();
    void ecefVelocityCoriolis();
};

void TstDoppler::signAndMagnitude()
{
    const std::array<double, 3> sta = { 0.0, 0.0, 0.0 };
    const double f = 1450.0;   // 常用业余/气象下行频段

    // 沿 +X 径向靠近观测站 (v=−7.5 km/s) → 正频移, |Δf| = v_r/c·f
    auto near = ecefToTeme({ 8000.0, 0.0, 0.0 }, { -7.5, 0.0, 0.0 }, JD);
    const double dNear = DopplerCalc::compute(near.first, near.second, JD, sta, f);
    const double expect = 7.5 / C_KM_S * f * 1000.0;   // ≈ 36.3 kHz
    QVERIFY2(dNear > 0.0, qPrintable(QString("靠近应为正频移: %1 kHz").arg(dNear, 0, 'f', 4)));
    QVERIFY(std::abs(dNear - expect) < 0.02);

    // 远离 → 负频移, 幅度一致
    auto far = ecefToTeme({ 8000.0, 0.0, 0.0 }, { 7.5, 0.0, 0.0 }, JD);
    const double dFar = DopplerCalc::compute(far.first, far.second, JD, sta, f);
    QVERIFY2(dFar < 0.0, qPrintable(QString("远离应为负频移: %1 kHz").arg(dFar, 0, 'f', 4)));
    QVERIFY(std::abs(dFar + expect) < 0.02);
}

void TstDoppler::geostationaryZeroShift()
{
    const std::array<double, 3> sta = { 0.0, 0.0, 0.0 };
    const double f = 1450.0;

    // 同步轨道卫星在 ECEF 中静止 → 相对地面观测站径向速度为零
    // （若哥里奥利项符号错误, 此处会得到 ~3 km/s 级别的伪频移）
    auto geo = ecefToTeme({ 42164.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, JD);
    const double dGeo = DopplerCalc::compute(geo.first, geo.second, JD, sta, f);
    QVERIFY2(std::abs(dGeo) < 1e-6,
             qPrintable(QString("同步轨道应零频移: %1 kHz").arg(dGeo, 0, 'f', 9)));
}

void TstDoppler::ecefVelocityCoriolis()
{
    // 直接验证 eciStateToEcef 的速度变换: vE = Rz(th)·vT − ω×rE
    const std::array<double, 3> rE = { 7000.0, 1000.0, 3000.0 };
    const std::array<double, 3> vE = { 1.2, -0.5, 0.8 };   // 任意 ECEF 速度
    auto state = ecefToTeme(rE, vE, JD);
    const auto out = CoordTransform::eciStateToEcef(state.first, state.second, JD);

    for (int i = 0; i < 3; ++i)
        QVERIFY(std::abs(out.first[i] - rE[i]) < 1e-6);   // 位置往返
    for (int i = 0; i < 3; ++i)
        QVERIFY(std::abs(out.second[i] - vE[i]) < 1e-6);  // 速度往返
                                        // 容忍 ω 值 (7.292115e-5 vs IERS) 微小差异
}

int runTstDoppler(int argc, char *argv[])
{
    TstDoppler t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_doppler.moc"
