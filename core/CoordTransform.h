#ifndef CORE_COORDTRANSFORM_H
#define CORE_COORDTRANSFORM_H
// =============================================================================
// CoordTransform.h — 坐标转换工具集
// GMST | TEME(ECI) ↔ ECEF | ECEF ↔ 大地经纬高 | ENU 站心 | 大气折射
// 所有函数为纯静态方法，无状态
// =============================================================================
#include "models.h"
#include <array>

class CoordTransform {
public:
    // WGS-84 椭球参数
    static constexpr double WGS84_A_KM       = 6378.137;       // 半长轴 km
    static constexpr double WGS84_F_INV      = 298.257223563;  // 扁率倒数
    static constexpr double WGS84_E2         = 2.0 / WGS84_F_INV
                                             - 1.0 / (WGS84_F_INV * WGS84_F_INV);

    // ---- GMST 格林尼治平恒星时 ----
    // jdUt1: 儒略日（UT1，可用 UTC 近似，差值 < 0.9s）
    // 返回: GMST 弧度，范围 [0, 2π)
    static double gmstRad(double jdUt1);

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
    static LookAngle computeLookAngle(
        const std::array<double, 3> &satECEF,
        const std::array<double, 3> &staECEF,
        double staLatDeg, double staLonDeg);

    // ---- 大气折射修正（Bennett 1982） ----
    // elGeomDeg: 几何仰角（度），必须 ≥ 0°
    // 返回: 视仰角（度），已施加大气折射
    static double applyRefraction(double elGeomDeg);
};

#endif // CORE_COORDTRANSFORM_H
