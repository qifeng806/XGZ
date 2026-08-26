// =============================================================================
// Observer.cpp — 地面站实现
// =============================================================================
#include "Observer.h"
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
        if (mLatDeg >= 90.0) mLatDeg = 90.0;
        if (mLatDeg <= -90.0) mLatDeg = -90.0;
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
        // 记录错误信息（在实际应用中可以使用日志系统）
        // std::cerr << "观测站初始化失败: " << e.what() << std::endl;

        // 回退到赤道 (0,0) 海平面站, 并由该回退参数【重新生成】ECEF——
        // 若直接置 mEcef={0,0,0} 会得到地心坐标, 与 (0,0,0) 站的真实
        // ECEF≈(6378.137,0,0) 不自洽, lookAngle 几何全部失真
        mLatDeg = 0.0;
        mLonDeg = 0.0;
        mAltKm = 0.0;
        mMinEl = 0.0;
        mEcef = CoordTransform::geodeticToEcef(GeoPos(mLatDeg, mLonDeg, mAltKm));
    }
}

LookAngle Observer::lookAngle(const std::array<double, 3> &satECEF) const
{
    try {
        // 检查输入坐标有效性
        if (!std::isfinite(satECEF[0]) || !std::isfinite(satECEF[1]) || !std::isfinite(satECEF[2])) {
            throw std::invalid_argument("卫星ECEF坐标无效");
        }
        
        return CoordTransform::computeLookAngle(satECEF, mEcef, mLatDeg, mLonDeg);
    } catch (const std::exception &e) {
        // 记录错误信息（在实际应用中可以使用日志系统）
        // std::cerr << "观测角度计算失败: " << e.what() << std::endl;
        
        // 失败回退 -91°: 明确低于地平线, 与 ELEV_FAILURE_SENTINEL 的
        // "失败永不参与 AOS/LOS 边界检测"语义一致（0° 是合法仰角值）
        return LookAngle(-91.0, 0.0, 0.0);
    }
}

GeoPos Observer::subSatPoint(const std::array<double, 3> &satECEF) const
{
    try {
        // 检查输入坐标有效性
        if (!std::isfinite(satECEF[0]) || !std::isfinite(satECEF[1]) || !std::isfinite(satECEF[2])) {
            throw std::invalid_argument("卫星ECEF坐标无效");
        }
        
        GeoPos result = CoordTransform::ecefToGeodetic(satECEF);
        
        // 检查结果有效性
        if (!std::isfinite(result.latDeg) || !std::isfinite(result.lonDeg) || !std::isfinite(result.altKm)) {
            throw std::invalid_argument("地理坐标转换失败");
        }
        
        return result;
    } catch (const std::exception &e) {
        // 记录错误信息（在实际应用中可以使用日志系统）
        // std::cerr << "星下点计算失败: " << e.what() << std::endl;
        
        // 返回默认值
        return GeoPos(0.0, 0.0, 0.0);
    }
}
