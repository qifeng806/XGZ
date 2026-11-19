#ifndef CORE_COORDTRANSFORM_H
#define CORE_COORDTRANSFORM_H
// =============================================================================
// CoordTransform.h — 坐标转换工具集
// GMST | TEME(ECI) ↔ ECEF | ECEF ↔ 大地经纬高 | ENU 站心（纯几何, 无折射）
// 所有函数为纯静态方法，无状态
// =============================================================================
#include "DataTypes.h"
#include <array>
#include <utility>

class CoordTransform {
public:
    // WGS-84 椭球参数
    static constexpr double WGS84_A_KM       = 6378.137;       // 半长轴 km
    static constexpr double WGS84_F_INV      = 298.257223563;  // 扁率倒数
    static constexpr double WGS84_E2         = 2.0 / WGS84_F_INV
                                             - 1.0 / (WGS84_F_INV * WGS84_F_INV);

    // ---- GMST 记忆化 ----
    // 实现为 thread_local 单条目缓存（键 = "当天 0h 的儒略日"），O(1) 无扫描。
    // 旧实现为 1000 项 FIFO + 容差 1e-8 天的线性扫描：扫掠调用下几乎全不命中
    // 且每次未命中都要扫满 1000 项，实测比不计算更慢（详见 .cpp 注释）

    // ---- GMST 格林尼治平恒星时 ----
    // jdUt1: 儒略日（UT1，可用 UTC 近似，差值 < 0.9s）
    // 返回: GMST 弧度，范围 [0, 2π)
    static double gmstRad(double jdUt1);

    // ---- ΔT = TT − UT1（秒）----
    // Espenak & Meeus (2006)/NASA deltatpoly2004 分段多项式:
    //   1900~2050 各分段 + 2050~2150 段（含 −0.5628·(2150−y) 连续性修正项）
    //   + ≥2150 长期抛物线; <1900 取 1900 边界值（−2.79 s）
    // 仅用于太阳/行星历表（TT 时标）: jdTT = jdUtc + ΔT/86400
    // 注意: GMST/地球自转需要 UT1，而 UT1 ≈ UTC（≤ 0.9s），不可加 ΔT
    static double deltaTSeconds(double jdUtc);

    // ---- TEME(ECI) → ECEF ----
    // 绕 Z 轴旋转 +GMST 角（将惯性系旋转到与地球同步）
    // 输入: posTeme — TEME 系位置 km; jdUt1 — 儒略日
    // 返回: ECEF 位置 km
    static std::array<double, 3> eciToEcef(
        const std::array<double, 3> &posTeme, double jdUt1);

    // ---- 方向矢量 ECI → ECEF（仅旋转 GMST，不平移） ----
    // 用于太阳方向等单位矢量：必须与卫星位置处于同一坐标系才能做点积
    static std::array<double, 3> eciDirToEcef(
        const std::array<double, 3> &dirTeme, double jdUt1);

    // ---- ECEF → 大地经纬高（Bowring 1976 迭代法） ----
    static GeoPos ecefToGeodetic(const std::array<double, 3> &ecef);

    // ---- 大地经纬高 → ECEF ----
    static std::array<double, 3> geodeticToEcef(const GeoPos &geo);

    // ---- 站心 ENU 分量 ----
    // relVec = satECEF − staECEF，输入为差值矢量
    // 返回 (East, North, Up) 分量 km
    static std::array<double, 3> ecefToEnuComponents(
        const std::array<double, 3> &relVec,
        double staLatDeg, double staLonDeg);

    // ---- 整站: 卫星 ECEF + 站 ECEF + 站经纬 → LookAngle ----
    // 输出为纯几何量（不含大气折射）: 与 STK AER 报告同定义
    static LookAngle computeLookAngle(
        const std::array<double, 3> &satECEF,
        const std::array<double, 3> &staECEF,
        double staLatDeg, double staLonDeg);
};

#endif // CORE_COORDTRANSFORM_H
