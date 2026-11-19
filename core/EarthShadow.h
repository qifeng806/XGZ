#ifndef CORE_EARTHSHADOW_H
#define CORE_EARTHSHADOW_H
// =============================================================================
// EarthShadow.h — 卫星地影判定（圆锥阴影模型: 本影 / 半影 / 全日照）
//
// 与 STK 11 的 Lighting 口径一致: STK 把光照分为**三态**，并以两条界线描述
//   - Sunlight / Penumbra 界线（太阳圆面恰好被完全遮挡之外）
//   - Penumbra / Umbra 界线
//（STK 帮助: STK Objects > Constraints > Lighting Conditions 列出
//  "Direct Sun / Penumbra or Direct Sun / Penumbra / Penumbra or Umbra /
//   Umbra / Umbra or Direct Sun" 六种可约束组合; 2D 图形可显示两条界线）。
// 因此本工程返回**太阳圆面可见比例 f ∈ [0, 1]**（1=全日照, 0=本影,
// 0<f<1=半影），而不是"有影/无影"的硬二值——二值化会丢掉 STK 的半影状态。
//
// 几何（θ = 地心-卫星矢量与反太阳方向的夹角）:
//   视地球半径 ρe = asin(Re/|r|), 视太阳半径 ρs = asin(Rs/|r−r_sun|)
//   θ ≥ ρe + ρs          → f = 1（全日照）
//   θ ≤ ρe − ρs          → f = 0（本影）
//   其间                 → f = 太阳圆面未被地球遮挡的面积比（半影）
// =============================================================================
#include "DataTypes.h"
#include <array>

class EarthShadow {
public:
    // 地球半径（与 STK 的 SGP4/光照几何同量级取赤道半径）
    static constexpr double EARTH_RADIUS_KM  = 6378.1363;
    // 太阳半径 km
    static constexpr double SUN_RADIUS_KM    = 695700.0;
    // 1 天文单位 km
    static constexpr double AU_KM            = 149597870.7;

    // satECEF: 卫星 ECEF 位置 (km)
    // sunDirECEF: 太阳地心方向单位矢量（ECEF 地固系）
    //             注意：必须先经 CoordTransform::eciDirToEcef 将太阳方向
    //             旋转到地固系，与卫星位置同系才能做点积（差一个 GMST 旋转，
    //             对 MEO/GEO 可差数十度，不能忽略）
    // sunDistAU: 日地距离（天文单位），≤0 时按 1.0
    //
    // 三态判定: f ≤ 1e-9 → InUmbra, f ≥ 1−1e-9 → FullSunlight,
    //           其余 → InPenumbra（与 STK Lighting 的三态一一对应）
    static ShadowStatus checkShadow(const std::array<double, 3> &satECEF,
                                    const std::array<double, 3> &sunDirECEF,
                                    double sunDistAU = 1.0);

    // 太阳圆面可见比例 f ∈ [0, 1]（1=全日照, 0=本影, 中间=半影）
    // 非有限输入按"失败即不可见"返回 0.0（纵深防御, 上游已过滤非有限值）
    static double umbraFactor(const std::array<double, 3> &satECEF,
                              const std::array<double, 3> &sunDirECEF,
                              double sunDistAU = 1.0);
};

#endif // CORE_EARTHSHADOW_H
