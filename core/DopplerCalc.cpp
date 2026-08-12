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
    // ---- Step 1: TEME → ECEF 位置+速度（含地球自转哥里奥利修正） ----
    // 旋转角需要 UT1；UT1 ≈ UTC（≤ 0.9s），直接使用 UTC 儒略日，
    // 不加 ΔT（ΔT = TT−UT1，加了会把地球自转角系统性偏大 ~0.29°）
    const auto ecef = CoordTransform::eciStateToEcef(rTeme, vTeme, jd);
    const auto &rECEF = ecef.first;
    const auto &vECEF = ecef.second;

    // ---- Step 2: LOS 单位矢量 ----
    double dx = rECEF[0] - staECEF[0];
    double dy = rECEF[1] - staECEF[1];
    double dz = rECEF[2] - staECEF[2];
    double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 1e-9) return 0.0;
    const double lx = dx / dist, ly = dy / dist, lz = dz / dist;

    // ---- Step 3: 径向速度（靠近为负 → 按照公式 doppler = −v_radial / c × f） ----
    double vRadial = vECEF[0] * lx + vECEF[1] * ly + vECEF[2] * lz;

    // ---- Step 4: 多普勒频移（kHz） ----
    // 正 = 靠近（频率升高）
    return -vRadial / SPEED_OF_LIGHT_KM_S * carrierFreqMHz * 1000.0;
}
