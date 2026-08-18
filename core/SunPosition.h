#ifndef CORE_SUNPOSITION_H
#define CORE_SUNPOSITION_H
// =============================================================================
// SunPosition.h — Meeus 低精度太阳星历
// 给定 JD 和观测站经纬度 → 输出太阳方位角/高度角/RA/Dec/日地距离
// 精度约 0.01°（1950–2050 年范围），足够过境可见性判定
// =============================================================================
#include "models.h"
#include <optional>

class SunPositionCalc {
public:
    // 输入/中间值非法（NaN/Inf/距离非正等）→ 返回 nullopt，调用侧可区分"真失败"
    // 与"合法结果"，而非吞错返回默认值
    static std::optional<SunPosition> compute(const double jd,
                                              double obsLatDeg, double obsLonDeg);
};

#endif // CORE_SUNPOSITION_H
