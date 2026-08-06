// =============================================================================
// EarthShadow.cpp — 圆柱形地影近似判定
// =============================================================================
#include "EarthShadow.h"
#include <cmath>

ShadowStatus EarthShadow::checkShadow(const std::array<double, 3> &satECEF,
                                       const std::array<double, 3> &sunDirECEF)
{
    // 圆柱形地影近似:
    // 地影 = 以地心-太阳连线为轴、半径 R_earth 的无限长圆柱体
    //
    // 条件1: 卫星在夜侧（卫星到太阳方向的分量为负 → 地心-卫星矢量和太阳方向夹角 > 90°）
    // 条件2: 卫星到日地轴线的垂距 < R_earth
    //
    // 注意: satECEF 与 sunDirECEF 必须在同一坐标系（地固系）

    double dotProduct = satECEF[0] * sunDirECEF[0]
                      + satECEF[1] * sunDirECEF[1]
                      + satECEF[2] * sunDirECEF[2];

    // 如果 dotProduct >= 0，卫星在日侧，完全日照
    if (dotProduct >= 0.0) {
        return ShadowStatus::FullSunlight;
    }

    // 卫星在夜侧：计算卫星到日地轴线的垂距
    double satDist2 = satECEF[0] * satECEF[0]
                    + satECEF[1] * satECEF[1]
                    + satECEF[2] * satECEF[2];
    double dPerp2 = satDist2 - dotProduct * dotProduct;

    if (dPerp2 < 0.0) dPerp2 = 0.0;
    double dPerp = std::sqrt(dPerp2);

    if (dPerp < EARTH_RADIUS_KM) {
        return ShadowStatus::InUmbra;
    }
    return ShadowStatus::FullSunlight;
}
