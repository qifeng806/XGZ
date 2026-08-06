// =============================================================================
// DopplerCalc.cpp — 多普勒频移实现
// =============================================================================
#include "DopplerCalc.h"
#include "CoordTransform.h"
#include <cmath>

double DopplerCalc::compute(const std::array<double, 3> &rTeme,
                              const std::array<double, 3> &vTeme,
                              double jd,
                              const std::array<double, 3> &staECEF,
                              double carrierFreqMHz)
{
    // ---- Step 1: TEME → ECEF 位置 ----
    auto rECEF = CoordTransform::eciToEcef(rTeme, jd);

    // ---- Step 2: TEME → ECEF 速度（含地球自转修正） ----
    double gmst = CoordTransform::gmstRad(jd);
    double c = std::cos(gmst), s = std::sin(gmst);
    // 惯性速度 → 旋转到 ECEF
    double vxECI = vTeme[0], vyECI = vTeme[1], vzECI = vTeme[2];
    double vxECEF =  vxECI * c + vyECI * s;
    double vyECEF = -vxECI * s + vyECI * c;
    double vzECEF = vzECI;

    // 地球自转角速度（rad/s）
    constexpr double omegaEarth = 7.292115e-5;
    // 哥里奥利：v_ECEF = v_ECI_rotated − ω × r_ECEF
    // ω × r_ECEF = (0, 0, omegaEarth) × (rx, ry, rz) = (−omegaEarth*ry, omegaEarth*rx, 0)
    vxECEF += omegaEarth * rECEF[1];
    vyECEF -= omegaEarth * rECEF[0];

    // ---- Step 3: LOS 单位矢量 ----
    double dx = rECEF[0] - staECEF[0];
    double dy = rECEF[1] - staECEF[1];
    double dz = rECEF[2] - staECEF[2];
    double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 1e-9) return 0.0;
    double lx = dx / dist, ly = dy / dist, lz = dz / dist;

    // ---- Step 4: 径向速度（靠近为负 → 按照公式 doppler = −v_radial / c × f） ----
    double vRadial = vxECEF * lx + vyECEF * ly + vzECEF * lz;

    // ---- Step 5: 多普勒频移（kHz） ----
    // 正 = 靠近（频率升高）
    return -vRadial / SPEED_OF_LIGHT_KM_S * carrierFreqMHz * 1000.0;
}
