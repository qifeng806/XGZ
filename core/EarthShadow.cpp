// =============================================================================
// EarthShadow.cpp — 圆锥形地影判定实现
// =============================================================================
#include "EarthShadow.h"
#include <cmath>

namespace {
    // 夹取到 [-1, 1]（防 asin/acos 越界 → NaN）
    inline double clampUnit(double x)
    {
        if (x > 1.0) return 1.0;
        if (x < -1.0) return -1.0;
        return x;
    }
}

double EarthShadow::umbraFactor(const std::array<double, 3> &satECEF,
                                const std::array<double, 3> &sunDirECEF,
                                double sunDistAU)
{
    // NaN/Inf 防御: 非有限输入若放行, 三分支比较对 NaN 全 false 会被
    // checkShadow 误判为 InPenumbra/半影——按「失败即不可见」原则返回
    // 完全遮挡（与 propagate 失败点的消费语义一致; 纵深防御, 生产链路
    // 上游 propagate/eciToEcef 已过滤非有限值）
    if (!std::isfinite(satECEF[0]) || !std::isfinite(satECEF[1]) || !std::isfinite(satECEF[2])
        || !std::isfinite(sunDirECEF[0]) || !std::isfinite(sunDirECEF[1]) || !std::isfinite(sunDirECEF[2])
        || !std::isfinite(sunDistAU))
        return 0.0;

    // ---- 日侧判定: 卫星到太阳方向分量为正 → 夹角 < 90° → 全日照 ----
    const double dotProduct = satECEF[0] * sunDirECEF[0]
                            + satECEF[1] * sunDirECEF[1]
                            + satECEF[2] * sunDirECEF[2];

    const double r = std::sqrt(satECEF[0] * satECEF[0]
                             + satECEF[1] * satECEF[1]
                             + satECEF[2] * satECEF[2]);
    if (r < 1e-9) return 1.0;   // 地心点无意义，按日照

    // cos(theta): theta = 地心-卫星矢量与反太阳方向的夹角（夜侧为 0°）
    // theta = 90° ↔ 卫星在地平圆上与太阳连线垂直
    const double cosTheta = clampUnit(-dotProduct / r);
    if (cosTheta <= 0.0) return 1.0;   // 日侧

    // ---- 视半径 ----
    const double dsun = (sunDistAU > 0.0 ? sunDistAU : 1.0) * AU_KM;
    const double rhoEarth = std::asin(clampUnit(EARTH_RADIUS_KM / r));
    const double rhoSun   = std::asin(clampUnit(SUN_RADIUS_KM / dsun));

    // ---- 半影带内线性过渡（theta ∈ [ρe−ρs, ρe+ρs]） ----
    const double theta = std::acos(cosTheta);
    const double frac = (theta - (rhoEarth - rhoSun)) / (2.0 * rhoSun);
    if (frac <= 0.0) return 0.0;
    if (frac >= 1.0) return 1.0;
    return frac;
}

ShadowStatus EarthShadow::checkShadow(const std::array<double, 3> &satECEF,
                                       const std::array<double, 3> &sunDirECEF,
                                       double sunDistAU)
{
    const double f = umbraFactor(satECEF, sunDirECEF, sunDistAU);
    // 1e-9 阈值复刻圆锥边界精确语义（见 tst_earthshadow::coneBoundaries）
    if (f <= 1e-9) return ShadowStatus::InUmbra;
    if (f >= 1.0 - 1e-9) return ShadowStatus::FullSunlight;
    return ShadowStatus::InPenumbra;
}
