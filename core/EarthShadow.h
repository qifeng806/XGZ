#ifndef CORE_EARTHSHADOW_H
#define CORE_EARTHSHADOW_H
// =============================================================================
// EarthShadow.h — 卫星地影判定（圆锥形模型）
// 用太阳视半径与地球视半径的几何关系区分本影/半影/全日照
// 参考 Vallado, "Fundamentals of Astrodynamics" §3.7（圆锥地影）
// =============================================================================
#include "models.h"
#include <array>

class EarthShadow {
public:
    // WGS84 赤道半径 km（圆锥母线半径）
    static constexpr double EARTH_RADIUS_KM  = 6378.137;
    // 太阳半径 km
    static constexpr double SUN_RADIUS_KM    = 695700.0;
    // 1 天文单位 km
    static constexpr double AU_KM            = 149597870.7;

    // satECEF: 卫星 ECEF 位置 (km)
    // sunDirECEF: 太阳地心方向单位矢量（ECEF 地固系）
    //             注意：必须先经 CoordTransform::eciDirToEcef 将太阳方向
    //             旋转到地固系，与卫星位置同系才能做点积（差一个 GMST 旋转，
    //             对 MEO/GEO 可差数十度，不能忽略）
    // sunDistAU: 日地距离（天文单位），用于计算太阳视半径；≤0 时按 1.0
    //
    // 模型几何（theta = 地心-卫星矢量与反太阳方向的夹角）:
    //   视地球半径 ρe = asin(Re/|r|)，视太阳半径 ρs = asin(Rs/dsun)
    //   theta < ρe − ρs        → 本影（地球完全遮住太阳）
    //   ρe − ρs ≤ theta < ρe + ρs → 半影（部分遮住）
    //   theta ≥ ρe + ρs（或日侧） → 全日照
    static ShadowStatus checkShadow(const std::array<double, 3> &satECEF,
                                    const std::array<double, 3> &sunDirECEF,
                                    double sunDistAU = 1.0);

    // 太阳圆面可见比例 [0, 1]：1=全日照，0=本影，
    // 半影带内随夹角 theta 从本影界到半影界线性过渡
    // 参数含义与 checkShadow 相同；用于光照强度/星上电源等细化场景
    static double umbraFactor(const std::array<double, 3> &satECEF,
                              const std::array<double, 3> &sunDirECEF,
                              double sunDistAU = 1.0);
};

#endif // CORE_EARTHSHADOW_H
