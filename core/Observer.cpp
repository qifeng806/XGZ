// =============================================================================
// Observer.cpp — 地面站实现
// =============================================================================
#include "Observer.h"
#include "CoordTransform.h"
#include <algorithm>
#include <cmath>

Observer::Observer(double latDeg, double lonDeg, double altKm, double minEl)
    : mLatDeg(latDeg), mLonDeg(lonDeg), mAltKm(altKm), mMinEl(minEl)
{
    // 构造时预计算 ECEF 坐标，后续所有 lookAngle 调用复用此值
    // 参数防御: 越界经纬度/负海拔会静默产生失真的站心几何——
    // 纬度钳到 [-90,90], 经度回绕到 [-180,180], 海拔钳到 [0,100]
    mLatDeg = std::clamp(mLatDeg, -90.0, 90.0);
    if (mLatDeg >= 90.0) mLatDeg = 90.0;
    if (mLatDeg <= -90.0) mLatDeg = -90.0;
    mLonDeg = std::fmod(mLonDeg + 180.0, 360.0);
    if (mLonDeg < 0.0) mLonDeg += 360.0;
    mLonDeg -= 180.0;
    mAltKm  = std::clamp(mAltKm, 0.0, 100.0);
    mMinEl  = std::clamp(mMinEl, 0.0, 90.0);
    mEcef = CoordTransform::geodeticToEcef(GeoPos(mLatDeg, mLonDeg, mAltKm));
}

LookAngle Observer::lookAngle(const std::array<double, 3> &satECEF) const
{
    return CoordTransform::computeLookAngle(satECEF, mEcef, mLatDeg, mLonDeg);
}

GeoPos Observer::subSatPoint(const std::array<double, 3> &satECEF) const
{
    return CoordTransform::ecefToGeodetic(satECEF);
}
