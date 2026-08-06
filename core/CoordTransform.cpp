// =============================================================================
// CoordTransform.cpp — 坐标转换完整实现
// =============================================================================
#include "CoordTransform.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
    constexpr double PI = M_PI;
    constexpr double TWOPI = 2.0 * PI;
    constexpr double DEG2RAD = PI / 180.0;
    constexpr double SEC_PER_DAY = 86400.0;

    inline double deg2rad(double d) { return d * DEG2RAD; }
    inline double rad2deg(double r) { return r / DEG2RAD; }
}

// ============================================================================
// gmstRad — IAU-1982 格林尼治平恒星时（弧度）
// ============================================================================
double CoordTransform::gmstRad(double jdUt1)
{
    // 当天 0h UT1 的儒略日
    double jd0 = std::floor(jdUt1 - 0.5) + 0.5;

    // J2000.0 起算的儒略世纪
    double T = (jd0 - 2451545.0) / 36525.0;

    // 当天 0h 的 GMST（秒）
    double gmstSec = 24110.54841
                   + 8640184.812866 * T
                   + 0.093104 * T * T
                   - 6.2e-6 * T * T * T;

    // 加上当天 UT1 小数的贡献（恒星日 / 太阳日 ≈ 1.00273790935）
    double fracDay = (jdUt1 - jd0);
    gmstSec += SEC_PER_DAY * fracDay * 1.00273790935;

    // 取模 → 转为弧度 → 正规化到 [0, 2π)
    gmstSec = std::fmod(gmstSec, SEC_PER_DAY);
    if (gmstSec < 0.0) gmstSec += SEC_PER_DAY;

    double gmstRad = gmstSec * TWOPI / SEC_PER_DAY;
    return gmstRad;
}

// ============================================================================
// eciToEcef — TEME → ECEF（绕 Z 轴旋转 GMST 角）
// ============================================================================
std::array<double, 3> CoordTransform::eciToEcef(
    const std::array<double, 3> &posTeme, double jdUt1)
{
    double theta = gmstRad(jdUt1);
    double c = std::cos(theta);
    double s = std::sin(theta);

    return {
        posTeme[0] * c + posTeme[1] * s,
       -posTeme[0] * s + posTeme[1] * c,
        posTeme[2]
    };
}

// ============================================================================
// eciDirToEcef — 方向单位矢量 ECI → ECEF（仅旋转 GMST）
// ============================================================================
std::array<double, 3> CoordTransform::eciDirToEcef(
    const std::array<double, 3> &dirTeme, double jdUt1)
{
    double theta = gmstRad(jdUt1);
    double c = std::cos(theta);
    double s = std::sin(theta);

    return {
        dirTeme[0] * c + dirTeme[1] * s,
       -dirTeme[0] * s + dirTeme[1] * c,
        dirTeme[2]
    };
}

// ============================================================================
// ecefToGeodetic — Bowring 1976 迭代法: ECEF → WGS84 经纬高
// ============================================================================
GeoPos CoordTransform::ecefToGeodetic(const std::array<double, 3> &p)
{
    double x = p[0], y = p[1], z = p[2];
    double r = std::sqrt(x * x + y * y);

    // WGS84 椭球参数
    double a  = WGS84_A_KM;
    double f  = 1.0 / WGS84_F_INV;
    double b  = a * (1.0 - f);
    double e2 = WGS84_E2;
    double ep2 = e2 / (1.0 - e2);  // e'² = e²/(1−e²)

    // 经度（直接解析公式）
    double lonDeg = std::atan2(y, x) * 180.0 / PI;

    // 纬度 — Bowring 迭代
    double lat = std::atan2(z, (1.0 - e2) * r);  // 初始猜测

    for (int i = 0; i < 5; ++i) {
        double sinLat = std::sin(lat);
        double N = a / std::sqrt(1.0 - e2 * sinLat * sinLat);
        double latNew = std::atan2(z + ep2 * b * sinLat * sinLat * sinLat,
                                   r - e2 * N * std::cos(lat));
        if (std::abs(latNew - lat) < 1e-12) {
            lat = latNew;
            break;
        }
        lat = latNew;
    }

    double sinLat = std::sin(lat);
    double N = a / std::sqrt(1.0 - e2 * sinLat * sinLat);
    double altKm = r / std::cos(lat) - N;

    return GeoPos(lat * 180.0 / PI, lonDeg, altKm);
}

// ============================================================================
// geodeticToEcef — 大地经纬高 → ECEF
// ============================================================================
std::array<double, 3> CoordTransform::geodeticToEcef(const GeoPos &geo)
{
    double lat = deg2rad(geo.latDeg);
    double lon = deg2rad(geo.lonDeg);
    double alt = geo.altKm;

    double sinLat = std::sin(lat);
    double N = WGS84_A_KM / std::sqrt(1.0 - WGS84_E2 * sinLat * sinLat);

    return {
        (N + alt) * std::cos(lat) * std::cos(lon),
        (N + alt) * std::cos(lat) * std::sin(lon),
        ((1.0 - WGS84_E2) * N + alt) * sinLat
    };
}

// ============================================================================
// ecefToEnuComponents — ECEF 相对矢量 → 站心 (East, North, Up) 分量
// ============================================================================
std::array<double, 3> CoordTransform::ecefToEnuComponents(
    const std::array<double, 3> &d,
    double staLatDeg, double staLonDeg)
{
    double lat = deg2rad(staLatDeg);
    double lon = deg2rad(staLonDeg);

    double sLat = std::sin(lat), cLat = std::cos(lat);
    double sLon = std::sin(lon), cLon = std::cos(lon);

    return {
        -sLon * d[0] + cLon * d[1],                                            // East
        -sLat * cLon * d[0] - sLat * sLon * d[1] + cLat * d[2],               // North
         cLat * cLon * d[0] + cLat * sLon * d[1] + sLat * d[2]                // Up
    };
}

// ============================================================================
// computeLookAngle — 一站式计算仰角、方位角、斜距
// ============================================================================
LookAngle CoordTransform::computeLookAngle(
    const std::array<double, 3> &satECEF,
    const std::array<double, 3> &staECEF,
    double staLatDeg, double staLonDeg)
{
    // 相对矢量
    std::array<double, 3> d = {
        satECEF[0] - staECEF[0],
        satECEF[1] - staECEF[1],
        satECEF[2] - staECEF[2]
    };

    auto enu = ecefToEnuComponents(d, staLatDeg, staLonDeg);
    double e = enu[0], n = enu[1], u = enu[2];
    double range = std::sqrt(e * e + n * n + u * u);

    if (range < 1e-9) {
        return LookAngle(90.0, 0.0, 0.0);  // 卫星在头顶（理想情况）
    }

    double elDeg = std::asin(u / range) * 180.0 / PI;
    double azDeg = std::atan2(e, n) * 180.0 / PI;    // atan2(E,N): 0°=N, 90°=E
    if (azDeg < 0.0) azDeg += 360.0;

    return LookAngle(elDeg, azDeg, range);
}

// ============================================================================
// applyRefraction — 大气折射修正（Bennett 1982）
// ============================================================================
double CoordTransform::applyRefraction(double elGeomDeg)
{
    // Bennett 公式在负仰角下不适用（tan 会变号）
    if (elGeomDeg < 0.0) return elGeomDeg;

    // Bennett 1982: R(弧分) = 1 / tan(h + 7.31/(h + 4.4))
    // h = 仰角（度）
    double hRad = deg2rad(elGeomDeg + 7.31 / (elGeomDeg + 4.4));
    double R_arcmin = 1.0 / std::tan(hRad);
    return elGeomDeg + R_arcmin / 60.0;  // 弧分 → 度
}
