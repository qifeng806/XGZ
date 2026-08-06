#ifndef CORE_SUNPOSITION_H
#define CORE_SUNPOSITION_H
// =============================================================================
// SunPosition.h — Meeus 低精度太阳星历
// 给定 JD 和观测站经纬度 → 输出太阳方位角/高度角/RA/Dec/日地距离
// 精度约 0.01°（1950–2050 年范围），足够过境可见性判定
// =============================================================================
#include "models.h"

class SunPositionCalc {
public:
    static SunPosition compute(const double jd,
                               double obsLatDeg, double obsLonDeg);
};

#endif // CORE_SUNPOSITION_H
