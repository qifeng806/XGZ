// =============================================================================
// Observer.cpp — 地面站实现
// =============================================================================
#include "Observer.h"
#include "CoordTransform.h"

Observer::Observer(double latDeg, double lonDeg, double altKm, double minEl)
    : mLatDeg(latDeg), mLonDeg(lonDeg), mAltKm(altKm), mMinEl(minEl)
{
    // 构造时预计算 ECEF 坐标，后续所有 lookAngle 调用复用此值
    mEcef = CoordTransform::geodeticToEcef(GeoPos(latDeg, lonDeg, altKm));
}

LookAngle Observer::lookAngle(const std::array<double, 3> &satECEF) const
{
    return CoordTransform::computeLookAngle(satECEF, mEcef, mLatDeg, mLonDeg);
}

GeoPos Observer::subSatPoint(const std::array<double, 3> &satECEF) const
{
    return CoordTransform::ecefToGeodetic(satECEF);
}
