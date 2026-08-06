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

ShadowStatus EarthShadow::checkShadow(const std::array<double, 3> &satECEF,
                                       const std::array<double, 3> &sunDirECEF,
                                       double sunDistAU)
{
    // ---- 日侧判定: 卫星到太阳方向分量为正 → 夹角 < 90° → 全日照 ----
    const double dotProduct = satECEF[0] * sunDirECEF[0]
                            + satECEF[1] * sunDirECEF[1]
                            + satECEF[2] * sunDirECEF[2];

    const double r = std::sqrt(satECEF[0] * satECEF[0]
                             + satECEF[1] * satECEF[1]
                             + satECEF[2] * satECEF[2]);
    if (r < 1e-9) return ShadowStatus::FullSunlight;   // 地心点无意义，按日照

    // cos(theta): theta = 地心-卫星矢量与反太阳方向的夹角（夜侧为 0°）
    // theta = 90° ↔ 卫星在地平圆上与太阳连线垂直
    const double cosTheta = clampUnit(-dotProduct / r);
    if (cosTheta <= 0.0) return ShadowStatus::FullSunlight;   // 日侧

    // ---- 视半径 ----
    const double dsun = (sunDistAU > 0.0 ? sunDistAU : 1.0) * AU_KM;
    // 近地卫星 ρe 明显大于 ρs；对 GEO 亦成立（ρe≈8.7° ≫ ρs≈0.27°）
    const double rhoEarth = std::asin(clampUnit(EARTH_RADIUS_KM / r));
    const double rhoSun   = std::asin(clampUnit(SUN_RADIUS_KM / dsun));

    // ---- 圆锥判定 ----
    const double theta = std::acos(cosTheta);
    if (theta < rhoEarth - rhoSun)
        return ShadowStatus::InUmbra;
    if (theta < rhoEarth + rhoSun)
        return ShadowStatus::InPenumbra;
    return ShadowStatus::FullSunlight;
}
