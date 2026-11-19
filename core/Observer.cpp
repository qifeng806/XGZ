// =============================================================================
// Observer.cpp — 地面站实现
// =============================================================================
#include "Observer.h"
#include <limits>
#include "CoordTransform.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

Observer::Observer(double latDeg, double lonDeg, double altKm, double minEl)
    : mLatDeg(latDeg), mLonDeg(lonDeg), mAltKm(altKm), mMinEl(minEl)
{
    try {
        // 参数验证
        if (!std::isfinite(latDeg) || !std::isfinite(lonDeg) || 
            !std::isfinite(altKm) || !std::isfinite(minEl)) {
            throw std::invalid_argument("观测站参数无效");
        }
        
        // 构造时预计算 ECEF 坐标，后续所有 lookAngle 调用复用此值
        // 参数防御: 越界经纬度/负海拔会静默产生失真的站心几何——
        // 纬度钳到 [-90,90], 经度回绕到 [-180,180], 海拔钳到 [0,100]
        mLatDeg = std::clamp(mLatDeg, -90.0, 90.0);
        mLonDeg = std::fmod(mLonDeg + 180.0, 360.0);
        if (mLonDeg < 0.0) mLonDeg += 360.0;
        mLonDeg -= 180.0;
        mAltKm  = std::clamp(mAltKm, -1.0, 100.0);   // 下界与 UI spinbox 一致:
                                                      // 负海拔（死海/洼地）合法,
                                                      // horizonDipDeg 对负值钳 0 保守处理
        mMinEl  = std::clamp(mMinEl, 0.0, 90.0);
        
        // 检查转换后的ECEF坐标有效性
        mEcef = CoordTransform::geodeticToEcef(GeoPos(mLatDeg, mLonDeg, mAltKm));
        if (!std::isfinite(mEcef[0]) || !std::isfinite(mEcef[1]) || !std::isfinite(mEcef[2])) {
            throw std::invalid_argument("ECEF坐标转换失败");
        }
    } catch (const std::exception &e) {
        // 回退到赤道 (0,0) 海平面站
        mLatDeg = 0.0;
        mLonDeg = 0.0;
        mAltKm = 0.0;
        mMinEl = 0.0;
        mEcef = CoordTransform::geodeticToEcef(GeoPos(mLatDeg, mLonDeg, mAltKm));
    }
}

LookAngle Observer::lookAngle(const std::array<double, 3> &satECEF) const
{
    // 检查输入坐标有效性（直接返回 NaN, 与 CoordTransform 高效模式一致）
    if (!std::isfinite(satECEF[0]) || !std::isfinite(satECEF[1]) || !std::isfinite(satECEF[2])) {
        return LookAngle(std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN());
    }
    // 纯几何站心几何（仰角/方位角/斜距），与 STK AER 报告同定义
    return CoordTransform::computeLookAngle(satECEF, mEcef, mLatDeg, mLonDeg);
}

GeoPos Observer::subSatPoint(const std::array<double, 3> &satECEF) const
{
    // 检查输入坐标有效性（直接返回 NaN, 与系统级 NaN 哨兵约定对齐）
    if (!std::isfinite(satECEF[0]) || !std::isfinite(satECEF[1]) || !std::isfinite(satECEF[2])) {
        return GeoPos(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN());
    }

    GeoPos result = CoordTransform::ecefToGeodetic(satECEF);

    // 检查结果有效性（ecefToGeodetic 已内置 NaN 返回, 此处双重保险）
    if (!std::isfinite(result.latDeg) || !std::isfinite(result.lonDeg) || !std::isfinite(result.altKm)) {
        return GeoPos(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN());
    }

    return result;
}
