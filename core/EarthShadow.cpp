// =============================================================================
// EarthShadow.cpp — 圆锥地影判定实现（本影 / 半影 / 全日照, 与 STK Lighting 同口径）
// =============================================================================
#include "EarthShadow.h"
#include <algorithm>
#include <cmath>

namespace {
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
    // NaN/Inf 防御: 非有限输入若放行, 面积公式会对 NaN 全 false 而误判为
    // "半影"——按「失败即不可见」原则返回完全遮挡（与 propagate 失败点的
    // 消费语义一致; 生产链路上游已过滤非有限值）
    if (!std::isfinite(satECEF[0]) || !std::isfinite(satECEF[1]) || !std::isfinite(satECEF[2])
        || !std::isfinite(sunDirECEF[0]) || !std::isfinite(sunDirECEF[1]) || !std::isfinite(sunDirECEF[2])
        || !std::isfinite(sunDistAU))
        return 0.0;

    const double r = std::sqrt(satECEF[0] * satECEF[0]
                             + satECEF[1] * satECEF[1]
                             + satECEF[2] * satECEF[2]);
    if (r < 1e-9) return 1.0;   // 地心点无意义，按日照

    // 卫星到太阳的距离: 由**太阳地心矢量**（sunDirECEF × 日地距离）与卫星位置
    // 之差算出（精确式）。若直接用日地距离，视太阳半径会有 |r|/AU ≈ 5e-5 的
    // 相对偏差（LEO 折合半影跨越时刻约 0.05 ms——可忽略，但精确式无额外代价）
    const double sunDistKm = (sunDistAU > 0.0 ? sunDistAU : 1.0) * AU_KM;
    if (sunDistKm < 1e-9) return 1.0;
    const double dsx = sunDirECEF[0] * sunDistKm - satECEF[0];
    const double dsy = sunDirECEF[1] * sunDistKm - satECEF[1];
    const double dsz = sunDirECEF[2] * sunDistKm - satECEF[2];
    const double dsun = std::sqrt(dsx * dsx + dsy * dsy + dsz * dsz);
    if (dsun < 1e-9) return 1.0;

    // 卫星处所见的地球视半径 b 与太阳视半径 a（弧度）
    const double b = std::asin(clampUnit(EARTH_RADIUS_KM / r));
    const double a = std::asin(clampUnit(SUN_RADIUS_KM / dsun));

    // 地球中心方向与太阳方向的夹角 c（角距）
    const double dot = satECEF[0] * sunDirECEF[0]
                     + satECEF[1] * sunDirECEF[1]
                     + satECEF[2] * sunDirECEF[2];
    // 日侧: 夹角 > 90° → 地球不遮挡
    if (dot >= 0.0) return 1.0;
    const double c = std::acos(clampUnit(-dot / r));

    if (c >= a + b) return 1.0;                 // 两圆不相交 → 全日照
    if (c <= b - a) return 0.0;                 // 太阳圆面完全落入地球圆面 → 本影
    if (c <= a - b) return 1.0;                 // 地球圆面完全落入太阳圆面（理论不达, 防御）

    // 半影: 太阳圆面被地球圆面遮挡的面积比（圆-圆相交面积, 标准闭式解）
    const double x = (c * c + a * a - b * b) / (2.0 * c * a);
    const double y = (c * c + b * b - a * a) / (2.0 * c * b);
    const double t = std::sqrt(std::max(0.0,
        (-c + a + b) * (c + a - b) * (c - a + b) * (c + a + b)));
    const double blocked = a * a * std::acos(clampUnit(x))
                         + b * b * std::acos(clampUnit(y))
                         - 0.5 * t;
    const double f = 1.0 - blocked / (M_PI * a * a);
    return std::clamp(f, 0.0, 1.0);
}

ShadowStatus EarthShadow::checkShadow(const std::array<double, 3> &satECEF,
                                       const std::array<double, 3> &sunDirECEF,
                                       double sunDistAU)
{
    const double f = umbraFactor(satECEF, sunDirECEF, sunDistAU);
    if (f <= 1e-9)      return ShadowStatus::InUmbra;
    if (f >= 1.0 - 1e-9) return ShadowStatus::FullSunlight;
    return ShadowStatus::InPenumbra;
}
