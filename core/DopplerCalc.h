#ifndef CORE_DOPPLERCALC_H
#define CORE_DOPPLERCALC_H
// =============================================================================
// DopplerCalc.h — 多普勒频移计算
// 给定 TEME 位置/速度 + JD + 地面站 ECEF + 载波频率 → 输出频移 (kHz)
// 含地球自转项修正（将惯性速度转为 ECEF 速度）
// =============================================================================
#include <array>

class DopplerCalc {
public:
    static constexpr double SPEED_OF_LIGHT_KM_S = 299792.458;

    // rTeme: 卫星 TEME 位置 (km)
    // vTeme: 卫星 TEME 速度 (km/s)
    // jd: 儒略日
    // staECEF: 地面站 ECEF 坐标 (km)
    // carrierFreqMHz: 载波频率 (MHz)，默认 437.800 (ISS UHF)
    // 返回: 多普勒频移 (kHz)，正=靠近（频率升高），负=远离（频率降低）
    static double compute(const std::array<double, 3> &rTeme,
                          const std::array<double, 3> &vTeme,
                          double jd,
                          const std::array<double, 3> &staECEF,
                          double carrierFreqMHz = 437.800);
};

#endif // CORE_DOPPLERCALC_H
