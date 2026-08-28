#ifndef CORE_VISUALMAGNITUDE_H
#define CORE_VISUALMAGNITUDE_H
// =============================================================================
// VisualMagnitude.h — 卫星视星等估算（Vallerie 1963 相位模型）
// 公式: m = m₀ − 15 + 5·log10(R) − 2.5·log10(f(B))
//   m₀ = 固有星等（1000 km、满相位）; R = 观测者-卫星斜距(km)
//   f(B) = (sin B + (π − B)·cos B)/π  = 朝向观测者的受照圆面比例
//   B    = 相位角（卫星处"指向太阳"与"指向观测者"的夹角）
// m₀ 不在 TLE 中，需外部数据：内置常见亮星目录（近似值，参考 qsmag/McCants
// 星等数据库；Quicksat 假设 100% 照明、Molczan 假设 50%，差异约 0.8 星等），
// 未知卫星使用调用方提供的回退值（UI 可配置）。
// 说明: 估算精度 ±1~2 星等（受卫星几何姿态/表面反射率影响），仅作观测辅助。
// =============================================================================
#include <array>
#include <string>

class VisualMagnitudeCalc {
public:
    // 按名称子串匹配内置星等目录（不区分大小写思路: 目录项直接用大写）；
    // 未命中返回 fallback。卫星名来自 TLE 名称行或 NORAD 编号。
    static double standardMagnitude(const std::string &satName, double fallback = 4.5);

    // 相位角（弧度，[0, π]）: 卫星处"指向太阳"与"指向观测者"的单位矢量夹角。
    // 全部输入为 ECEF 地固系 km 坐标（太阳位置 = 地心方向 × 日地距离）。
    static double phaseAngleRad(const std::array<double, 3> &satEcefKm,
                                const std::array<double, 3> &sunEcefKm,
                                const std::array<double, 3> &obsEcefKm);

    // 视星等（越小越亮）。相位因子下限钳制 1e-6，防 log(0) → ±∞ 发散。
    static double visualMagnitude(double stdMag, double rangeKm, double phaseRad);
};

#endif // CORE_VISUALMAGNITUDE_H