// =============================================================================
// CoordTransform.cpp — 坐标转换完整实现
// =============================================================================
#include "CoordTransform.h"
#include <algorithm>
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
// 输入为 UT1 儒略日；可用 UTC 直接近似（UT1−UTC ≤ 0.9s），
// 切勿加 ΔT（ΔT = TT−UT1，加了得到 TT，地球自转角会系统性偏大 ~69s 恒星时）
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
// deltaTSeconds — ΔT = TT − UT1（秒）
// Espenak & Meeus (2006) 分段多项式, 覆盖 1900~2100 年
// 应用内历元范围之外时取边界值，保证连续
// ============================================================================
double CoordTransform::deltaTSeconds(double jdUtc)
{
    // 儒略日 → 年份（近似，足够 ΔT 使用）
    double year = 2000.0 + (jdUtc - 2451545.0) / 365.2425;

    if (year < 1900.0) {
        // 取 1900 年处边界值（t = y − 1900 = 0），保证连续
        return -2.79;
    }
    if (year < 1920.0) {
        double t = (year - 1900.0) / 100.0;
        return -2.79 + 1.494119 * t - 0.0598939 * t * t
             + 0.0061966 * t * t * t - 0.000197 * t * t * t * t;
    }
    if (year < 1941.0) {
        double t = year - 1920.0;
        return 21.20 + 0.84493 * t - 0.076100 * t * t
             + 0.0020936 * t * t * t;
    }
    if (year < 1961.0) {
        double t = year - 1950.0;
        return 29.07 + 0.407 * t - t * t / 233.0 + t * t * t / 2547.0;
    }
    if (year < 1986.0) {
        double t = year - 1975.0;
        return 45.45 + 1.067 * t - t * t / 260.0 - t * t * t / 718.0;
    }
    if (year < 2005.0) {
        double t = year - 2000.0;
        return 63.86 + 0.3345 * t - 0.060374 * t * t
             + 0.0017275 * t * t * t + 0.000651814 * t * t * t * t
             + 0.00002373599 * t * t * t * t * t;
    }
    if (year < 2050.0) {
        double t = year - 2000.0;
        return 62.92 + 0.32217 * t + 0.005589 * t * t;
    }
    // ≥ 2050（含 extrapolate 2100 后）
    double t = (year - 1820.0) / 100.0;
    return -20.0 + 32.0 * t * t - 0.5628 * (2150.0 - year);
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
// eciStateToEcef — TEME 位置+速度 → ECEF（含地球自转哥里奥利项）
// ============================================================================
std::pair<std::array<double, 3>, std::array<double, 3>>
CoordTransform::eciStateToEcef(const std::array<double, 3> &posTeme,
                               const std::array<double, 3> &velTeme,
                               double jdUt1)
{
    double theta = gmstRad(jdUt1);
    double c = std::cos(theta);
    double s = std::sin(theta);

    std::array<double, 3> posEcef = {
        posTeme[0] * c + posTeme[1] * s,
       -posTeme[0] * s + posTeme[1] * c,
        posTeme[2]
    };

    // 惯性速度先旋转到地面系参考，再减去地球自转牵连速度:
    // v_ECEF = R(θ)·v_TEME − ω×r_ECEF
    // ω×r = (0, 0, ω)×(rx, ry, rz) = (−ω·ry, ω·rx, 0)
    constexpr double OMEGA_EARTH_RAD_S = 7.292115e-5;
    std::array<double, 3> velEcef = {
        velTeme[0] * c + velTeme[1] * s + OMEGA_EARTH_RAD_S * posEcef[1],
       -velTeme[0] * s + velTeme[1] * c - OMEGA_EARTH_RAD_S * posEcef[0],
        velTeme[2]
    };
    return { posEcef, velEcef };
}

// ============================================================================
// ecefToGeodetic — Bowring 1976 闭式解法: ECEF → WGS84 经纬高
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

    // 纬度 — Bowring 1976 闭式解（参数纬度 θ 一次性修正，精度 < 1e-9°）
    //   θ = atan(z·a / (r·b))          参数纬度
    //   φ = atan2(z + e'²·b·sin³θ, r − e²·a·cos³θ)
    if (r < 1e-12) {
        double latDeg = (z >= 0.0) ? 90.0 : -90.0;
        double altKm = std::fabs(z) - b;
        return GeoPos(latDeg, 0.0, altKm);
    }

    double theta = std::atan2(z * a, r * b);
    double sinT = std::sin(theta), cosT = std::cos(theta);
    double lat = std::atan2(z + ep2 * b * sinT * sinT * sinT,
                            r - e2 * a * cosT * cosT * cosT);

    // 高度/纬度固定点迭代（Bowring 闭式解做初值，2~3 次即收敛）
    // 极区 cos(φ)→0 时 r/cos(φ) 除零 → 改用 z 反解: h = z/sinφ − N(1−e²)
    for (int i = 0; i < 3; ++i) {
        double sinL = std::sin(lat);
        double cosL = std::cos(lat);
        double Nl = a / std::sqrt(1.0 - e2 * sinL * sinL);
        double h;
        if (std::fabs(cosL) > 1e-9)
            h = r / cosL - Nl;
        else
            h = z / std::sin(lat) - Nl * (1.0 - e2);   // 极点: z 与 sinφ 同号
        lat = std::atan2(z, r * (1.0 - e2 * Nl / (Nl + h)));
    }

    double sinLat = std::sin(lat);
    double N = a / std::sqrt(1.0 - e2 * sinLat * sinLat);
    double altKm;
    if (std::fabs(std::cos(lat)) > 1e-9)
        altKm = r / std::cos(lat) - N;
    else
        altKm = z / std::sin(lat) - N * (1.0 - e2);

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

    // 钳制防浮点舍入使 |u/range| 略超 1（卫星在天顶附近时 e、n 趋于 0）
    double elDeg = std::asin(std::clamp(u / range, -1.0, 1.0)) * 180.0 / PI;
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
