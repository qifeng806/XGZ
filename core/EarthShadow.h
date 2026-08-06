#ifndef CORE_EARTHSHADOW_H
#define CORE_EARTHSHADOW_H
// =============================================================================
// EarthShadow.h — 卫星地影判定（圆柱形近似）
// 判定卫星处于地球本影/完全日照中
// 圆柱形模型对 LEO 卫星出入影时刻误差 < 1s
// =============================================================================
#include "models.h"
#include <array>

class EarthShadow {
public:
    static constexpr double EARTH_RADIUS_KM = 6371.0;

    // satECEF: 卫星 ECEF 位置 (km)
    // sunDirECEF: 太阳地心方向单位矢量（ECEF 地固系）
    //             注意：必须先经 CoordTransform::eciDirToEcef 将太阳方向
    //             旋转到地固系，与卫星位置同系才能做点积（差一个 GMST 旋转，
    //             对 MEO/GEO 可差数十度，不能忽略）
    // 返回: 地影状态（圆柱模型仅区分本影/全日照，不判定半影）
    static ShadowStatus checkShadow(const std::array<double, 3> &satECEF,
                                    const std::array<double, 3> &sunDirECEF);
};

#endif // CORE_EARTHSHADOW_H
