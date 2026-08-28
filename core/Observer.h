#ifndef CORE_OBSERVER_H
#define CORE_OBSERVER_H
// =============================================================================
// Observer.h — 地面观测站
// 封装地面站 WGS84 坐标，预计算 ECEF 位置，提供站心观测接口
// =============================================================================
#include "DataTypes.h"
#include <array>

class Observer {
public:
    Observer(double latDeg, double lonDeg, double altKm = 0.0,
             double minElevationDeg = 10.0);

    // 访问器
    double latDeg()        const { return mLatDeg; }
    double lonDeg()        const { return mLonDeg; }
    double altKm()         const { return mAltKm; }
    double minElevDeg()    const { return mMinEl; }
    const std::array<double, 3>& ecef() const { return mEcef; }

    // 站心观测量（仰角、方位角、斜距）
    LookAngle lookAngle(const std::array<double, 3> &satECEF) const;

    // 星下点（从卫星 ECEF 直接解经纬高）
    GeoPos subSatPoint(const std::array<double, 3> &satECEF) const;

private:
    double mLatDeg, mLonDeg, mAltKm, mMinEl;
    std::array<double, 3> mEcef;    // 预计算的站 ECEF 坐标
};

#endif // CORE_OBSERVER_H
