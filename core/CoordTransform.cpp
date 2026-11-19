// =============================================================================
// CoordTransform.cpp — 坐标转换完整实现
// =============================================================================
#include "CoordTransform.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <vector>

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
    
    // GMST 记忆化: 以"当天 0h 的 JD"为键的**单条目**缓存（O(1)，无线性扫描）。
    // 时间扫掠（粗搜 15s 步长 + 二分/采样回访）下相邻查询的 jd0 大量重复，
    // 命中即省去多项式；不命中只重算一次。
    //
    // 历史教训（性能回归修复）: 曾用 1000 项 FIFO + 容差 1e-8 天的 vector
    // 线性扫描缓存。扫掠时相邻 JD 相差 ~1.7e-4 天（≫ 容差）→ 几乎全部未命中，
    // 而每次未命中仍需扫满 1000 项；实测比不缓存慢 25~27 倍，占粗搜单步
    // 耗时约 39%。单条目按日键命中率高且无扫描成本。
    thread_local double memoJd0 = std::numeric_limits<double>::quiet_NaN();
    thread_local double memoGmstSec0 = 0.0;
}

// ============================================================================
// gmstRad — IAU-1982 格林尼治平恒星时（弧度）
// 输入为 UT1 儒略日；可用 UTC 直接近似（UT1−UTC ≤ 0.9s），
// 切勿加 ΔT（ΔT = TT−UT1，加了得到 TT，地球自转角会系统性偏大 ~69s 恒星时）
//
// 为什么**不能**"升级"为 IAU-2006:
//   TEME 基于赤道/春分点框架，其定义与 SGP4 输出一致，必须用 IAU-1982 GMST
//   （Vallado"Revisiting Spacetrack Report #3"/AIAA 2006-6753 中 TOD↔PEF 即用
//   θ_GMST82；本工程内置 perturb 的 gstime_SGP4 亦用同一组系数）。
//   若误用 IAU-2006 的 ERA（基于 CIO）会引入 ERA−GMST ≈ 4612.156534″·T 的
//   偏差——2025 年即 1153″ ≈ 0.32° ≈ 地面上 36 km；即便只用 GMST06 表达式，
//   与 GMST82 的差异也仅 0.055″（1.7 m），收益远小于框架不一致的风险。
// ============================================================================
double CoordTransform::gmstRad(double jdUt1)
{
    // 输入验证（直接返回 NaN, 与相邻 eciDirToEcef 的高效模式一致,
    // 避免热路径中 throw/catch 的 10-100x 性能开销）
    if (!std::isfinite(jdUt1)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // 当天 0h UT1 的儒略日
    double jd0 = std::floor(jdUt1 - 0.5) + 0.5;

    // 记忆化: jd0 由 jdUt1 确定性导出, 精确相等才复用（无需容差）
    double gmstSec = 0.0;
    if (memoJd0 == jd0) {
        gmstSec = memoGmstSec0;
    } else {
        // J2000.0 起算的儒略世纪
        double T = (jd0 - 2451545.0) / 36525.0;

        // 检查T的有效性
        if (!std::isfinite(T)) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        // 当天 0h 的 GMST（秒）: IAU-1982 公式
        gmstSec = 24110.54841
                + 8640184.812866 * T
                + 0.093104 * T * T
                - 6.2e-6 * T * T * T;

        // 检查gmstSec的有效性
        if (!std::isfinite(gmstSec)) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        memoJd0 = jd0;
        memoGmstSec0 = gmstSec;
    }

    // 加上当天 UT1 小数的贡献（恒星日 / 太阳日 ≈ 1.00273790935）
    double fracDay = (jdUt1 - jd0);
    gmstSec += SEC_PER_DAY * fracDay * 1.00273790935;

    // 检查fracDay的有效性
    if (!std::isfinite(fracDay)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // 取模 → 转为弧度 → 正规化到 [0, 2π)
    gmstSec = std::fmod(gmstSec, SEC_PER_DAY);
    if (gmstSec < 0.0) gmstSec += SEC_PER_DAY;

    double gmstRadVal = gmstSec * TWOPI / SEC_PER_DAY;

    // 确保结果在有效范围内
    if (!std::isfinite(gmstRadVal)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // 规范化到[0, 2π)范围
    gmstRadVal = std::fmod(gmstRadVal, TWOPI);
    if (gmstRadVal < 0.0) gmstRadVal += TWOPI;

    return gmstRadVal;
}

// ============================================================================
// deltaTSeconds — ΔT = TT − UT1（秒）
// Espenak & Meeus (2006) 分段多项式（NASA deltatpoly2004）:
//   1900~2050 各分段 + 2050~2150 段（含 −0.5628·(2150−y) 连续性修正项）
//   + ≥2150 长期抛物线; <1900 取 1900 边界值
// 应用内历元范围之外时取边界值，保证连续
// ============================================================================
double CoordTransform::deltaTSeconds(double jdUtc)
{
    // 输入验证
    if (!std::isfinite(jdUtc)) {
        return std::numeric_limits<double>::quiet_NaN();  // NaN 哨兵
    }

    // 儒略日 → 年份（近似，足够 ΔT 使用）
    double year = 2000.0 + (jdUtc - 2451545.0) / 365.2425;
    
    // 检查年份的有效性
    if (!std::isfinite(year)) {
        return std::numeric_limits<double>::quiet_NaN();  // NaN 哨兵
    }

    if (year < 1900.0) {
        // 取 1900 年处边界值（t = y − 1900 = 0），保证连续
        return -2.79;
    }
    if (year < 1920.0) {
        // Espenak & Meeus 标准定义 t 以"年"计（t = y − 1900）,
        // 不做世纪缩放——误除 100 会让段内值停在 −2.79 附近,
        // 与下一段起点 21.20s 形成 ~24s 边界跳变
        double t = year - 1900.0;
        if (!std::isfinite(t)) return -2.79;  // 无效t值返回边界值
        return -2.79 + 1.494119 * t - 0.0598939 * t * t
             + 0.0061966 * t * t * t - 0.000197 * t * t * t * t;
    }
    if (year < 1941.0) {
        double t = year - 1920.0;
        if (!std::isfinite(t)) return 21.20;  // 无效t值返回边界值
        return 21.20 + 0.84493 * t - 0.076100 * t * t
             + 0.0020936 * t * t * t;
    }
    if (year < 1961.0) {
        double t = year - 1950.0;
        if (!std::isfinite(t)) return 29.07;  // 无效t值返回边界值
        double term1 = 29.07 + 0.407 * t;
        double term2 = -t * t / 233.0;
        double term3 = t * t * t / 2547.0;
        if (!std::isfinite(term2) || !std::isfinite(term3)) return term1;  // 无效项返回部分结果
        return term1 + term2 + term3;
    }
    if (year < 1986.0) {
        double t = year - 1975.0;
        if (!std::isfinite(t)) return 45.45;  // 无效t值返回边界值
        double term1 = 45.45 + 1.067 * t;
        double term2 = -t * t / 260.0;
        double term3 = -t * t * t / 718.0;
        if (!std::isfinite(term2) || !std::isfinite(term3)) return term1;  // 无效项返回部分结果
        return term1 + term2 + term3;
    }
    if (year < 2005.0) {
        double t = year - 2000.0;
        if (!std::isfinite(t)) return 63.86;  // 无效t值返回边界值
        double term1 = 63.86 + 0.3345 * t - 0.060374 * t * t;
        double term2 = 0.0017275 * t * t * t;
        double term3 = 0.000651814 * t * t * t * t;
        double term4 = 0.00002373599 * t * t * t * t * t;
        if (!std::isfinite(term2) || !std::isfinite(term3) || !std::isfinite(term4)) return term1;  // 无效项返回部分结果
        return term1 + term2 + term3 + term4;
    }
    if (year < 2050.0) {
        double t = year - 2000.0;
        if (!std::isfinite(t)) return 62.92;  // 无效t值返回边界值
        double term1 = 62.92 + 0.32217 * t;
        double term2 = 0.005589 * t * t;
        if (!std::isfinite(term2)) return term1;  // 无效项返回部分结果
        return term1 + term2;
    }
    // 2050 ~ 2150: Espenak & Meeus (2006) 分段式（NASA deltatpoly2004）
    //   ΔT = −20 + 32·u² − 0.5628·(2150 − year),  u = (year − 1820)/100
    // 末项用于消除 2050 处的不连续: 本段在 2050.0 处与 2005~2050 段同为
    // 93.00 s, 在 2150 处与长期式同为 328.48 s（缺该项会在 2050 跳变 +56.28 s）
    // ≥ 2150: 长期抛物线 ΔT = −20 + 32·u²（即下式的 term2 = 0）
    const double t = (year - 1820.0) / 100.0;
    if (!std::isfinite(t)) return -20.0;  // 无效t值返回边界值
    const double term1 = -20.0 + 32.0 * t * t;
    const double term2 = (year < 2150.0) ? -0.5628 * (2150.0 - year) : 0.0;
    if (!std::isfinite(term1) || !std::isfinite(term2)) return -20.0;
    return term1 + term2;
}

// ============================================================================
// eciToEcef — TEME → ECEF（绕 Z 轴旋转 GMST 角）
// ============================================================================
std::array<double, 3> CoordTransform::eciToEcef(
    const std::array<double, 3> &posTeme, double jdUt1)
{
    // 输入验证（直接返回 NaN, 避免热路径中 throw/catch 性能开销）
    if (!std::isfinite(jdUt1)) {
        return {NAN, NAN, NAN};
    }
    
    // 检查位置矢量
    if (!std::isfinite(posTeme[0]) || !std::isfinite(posTeme[1]) || !std::isfinite(posTeme[2])) {
        return {NAN, NAN, NAN};
    }

    double theta = gmstRad(jdUt1);
    if (!std::isfinite(theta)) {
        return {NAN, NAN, NAN};
    }
    
    double c = std::cos(theta);
    double s = std::sin(theta);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(c) || !std::isfinite(s)) {
        return {NAN, NAN, NAN};
    }

    double x = posTeme[0] * c + posTeme[1] * s;
    double y = -posTeme[0] * s + posTeme[1] * c;
    double z = posTeme[2];

    // 检查结果的有效性
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return {NAN, NAN, NAN};
    }

    return {x, y, z};
}

// ============================================================================
// eciDirToEcef — 方向单位矢量 ECI → ECEF（仅旋转 GMST）
// ============================================================================
std::array<double, 3> CoordTransform::eciDirToEcef(
    const std::array<double, 3> &dirTeme, double jdUt1)
{
    // 输入验证（失败一律 NaN 哨兵: 与真实方向矢量可区分, NaN 比较自然拦截）
    if (!std::isfinite(jdUt1)) {
        return {NAN, NAN, NAN};  // 无效输入返回 NaN
    }
    
    // 检查方向矢量
    if (!std::isfinite(dirTeme[0]) || !std::isfinite(dirTeme[1]) || !std::isfinite(dirTeme[2])) {
        return {NAN, NAN, NAN};  // 无效方向返回 NaN
    }

    double theta = gmstRad(jdUt1);
    if (!std::isfinite(theta)) {
        return {NAN, NAN, NAN};  // 无效角度返回 NaN
    }
    
    double c = std::cos(theta);
    double s = std::sin(theta);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(c) || !std::isfinite(s)) {
        return {NAN, NAN, NAN};  // 无效三角函数返回 NaN
    }

    double x = dirTeme[0] * c + dirTeme[1] * s;
    double y = -dirTeme[0] * s + dirTeme[1] * c;
    double z = dirTeme[2];

    // 检查结果的有效性
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return {NAN, NAN, NAN};  // 无效结果返回 NaN
    }

    return {x, y, z};
}

// ============================================================================
// ecefToGeodetic — Bowring 1976 闭式解法: ECEF → WGS84 经纬高
// ============================================================================
GeoPos CoordTransform::ecefToGeodetic(const std::array<double, 3> &p)
{
    double x = p[0], y = p[1], z = p[2];
    
    // 输入验证
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return GeoPos(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN());
    }

    double r = std::sqrt(x * x + y * y);

    // WGS84 椭球参数
    double a  = WGS84_A_KM;
    double f  = 1.0 / WGS84_F_INV;
    double b  = a * (1.0 - f);
    double e2 = WGS84_E2;
    double ep2 = e2 / (1.0 - e2);  // e'² = e²/(1−e²)

    // 经度（直接解析公式）
    double lonDeg = std::atan2(y, x) * 180.0 / PI;
    
    // 确保经度在有效范围内（保留 ±180 原值, 仅回绕越界值）
    if (lonDeg > 180.0) lonDeg -= 360.0;
    else if (lonDeg < -180.0) lonDeg += 360.0;

    // 纬度 — Bowring 1976 闭式解（参数纬度 θ 一次性修正，精度 < 1e-9°）
    //   θ = atan(z·a / (r·b))          参数纬度
    //   φ = atan2(z + e'²·b·sin³θ, r − e²·a·cos³θ)
    if (r < 1e-12) {
        double latDeg = (z >= 0.0) ? 90.0 : -90.0;
        double altKm = std::fabs(z) - b;
        return GeoPos(latDeg, lonDeg, altKm);
    }

    double theta = std::atan2(z * a, r * b);
    double sinT = std::sin(theta), cosT = std::cos(theta);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(sinT) || !std::isfinite(cosT)) {
        return GeoPos(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN());
    }

    double lat = std::atan2(z + ep2 * b * sinT * sinT * sinT,
                            r - e2 * a * cosT * cosT * cosT);
    
    // 检查纬度计算结果
    if (!std::isfinite(lat)) {
        return GeoPos(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN());
    }

    // 高度/纬度固定点迭代（Bowring 闭式解做初值，2~3 次即收敛）
    // 极区 cos(φ)→0 时 r/cos(φ) 除零 → 改用 z 反解: h = z/sinφ − N(1−e²)
    for (int i = 0; i < 3; ++i) {
        double sinL = std::sin(lat);
        double cosL = std::cos(lat);
        
        // 检查三角函数值的有效性
        if (!std::isfinite(sinL) || !std::isfinite(cosL)) {
            break;  // 无效计算退出迭代
        }
        
        double Nl = a / std::sqrt(1.0 - e2 * sinL * sinL);
        
        // 检查Nl的有效性
        if (!std::isfinite(Nl) || Nl <= 0.0) {
            break;  // 无效计算退出迭代
        }
        
        double h;
        if (std::fabs(cosL) > 1e-9) {
            h = r / cosL - Nl;
        } else {
            // 极点处理
            if (std::fabs(sinL) < 1e-12) {
                break;  // 极点附近数值不稳定
            }
            h = z / sinL - Nl * (1.0 - e2);   // 极点: z 与 sinφ 同号
        }
        
        // 检查高度计算的有效性
        if (!std::isfinite(h)) {
            break;  // 无效计算退出迭代
        }
        
        double new_lat = std::atan2(z, r * (1.0 - e2 * Nl / (Nl + h)));
        
        // 检查新纬度的有效性
        if (!std::isfinite(new_lat)) {
            break;  // 无效计算退出迭代
        }
        
        // 检查收敛性
        if (std::fabs(new_lat - lat) < 1e-12) {
            lat = new_lat;
            break;
        }
        
        lat = new_lat;
    }

    double sinLat = std::sin(lat);
    double N = a / std::sqrt(1.0 - e2 * sinLat * sinLat);
    double altKm;
    
    // 检查N的有效性
    if (!std::isfinite(N) || N <= 0.0) {
        altKm = 0.0;  // 无效N值使用零高度
    } else if (std::fabs(std::cos(lat)) > 1e-9) {
        altKm = r / std::cos(lat) - N;
    } else {
        // 极点处理
        if (std::fabs(sinLat) < 1e-12) {
            altKm = 0.0;  // 极点高度设为0
        } else {
            altKm = z / sinLat - N * (1.0 - e2);
        }
    }
    
    // 确保高度有效
    if (!std::isfinite(altKm)) {
        altKm = 0.0;
    }

    // 确保纬度在有效范围内
    double latDeg = lat * 180.0 / PI;
    latDeg = std::clamp(latDeg, -90.0, 90.0);

    return GeoPos(latDeg, lonDeg, altKm);
}

// ============================================================================
// geodeticToEcef — 大地经纬高 → ECEF
// ============================================================================
std::array<double, 3> CoordTransform::geodeticToEcef(const GeoPos &geo)
{
    // 输入验证
    if (!std::isfinite(geo.latDeg) || !std::isfinite(geo.lonDeg) || !std::isfinite(geo.altKm)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    // 确保纬度在有效范围内
    double latDeg = std::clamp(geo.latDeg, -90.0, 90.0);
    double lonDeg = geo.lonDeg;
    
    // 规范化经度到[-180, 180]范围（保留 ±180 原值, 仅回绕越界值）
    if (lonDeg > 180.0) lonDeg -= 360.0;
    else if (lonDeg < -180.0) lonDeg += 360.0;

    double lat = deg2rad(latDeg);
    double lon = deg2rad(lonDeg);
    double alt = geo.altKm;

    double sinLat = std::sin(lat);
    double cosLat = std::cos(lat);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(sinLat) || !std::isfinite(cosLat)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    // 计算卯酉圈曲率半径N，避免数值不稳定
    double e2_sin2 = WGS84_E2 * sinLat * sinLat;
    if (e2_sin2 >= 1.0) {
        // 纬度过大导致数值不稳定
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }
    
    double N = WGS84_A_KM / std::sqrt(1.0 - e2_sin2);
    
    // 检查N的有效性
    if (!std::isfinite(N) || N <= 0.0) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    // 计算各分量
    double cosLon = std::cos(lon);
    double sinLon = std::sin(lon);
    
    // 检查经度三角函数值的有效性
    if (!std::isfinite(cosLon) || !std::isfinite(sinLon)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    double N_plus_alt = N + alt;
    double x = N_plus_alt * cosLat * cosLon;
    double y = N_plus_alt * cosLat * sinLon;
    double z = ((1.0 - WGS84_E2) * N + alt) * sinLat;

    // 检查最终结果的有效性
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    return {x, y, z};
}

// ============================================================================
// ecefToEnuComponents — ECEF 相对矢量 → 站心 (East, North, Up) 分量
// ============================================================================
std::array<double, 3> CoordTransform::ecefToEnuComponents(
    const std::array<double, 3> &d,
    double staLatDeg, double staLonDeg)
{
    // 输入验证
    if (!std::isfinite(staLatDeg) || !std::isfinite(staLonDeg)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }
    
    // 检查相对矢量
    if (!std::isfinite(d[0]) || !std::isfinite(d[1]) || !std::isfinite(d[2])) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    // 确保纬度在有效范围内
    double latDeg = std::clamp(staLatDeg, -90.0, 90.0);
    double lonDeg = staLonDeg;
    
    // 规范化经度到[-180, 180]范围（保留 ±180 原值, 仅回绕越界值）
    if (lonDeg > 180.0) lonDeg -= 360.0;
    else if (lonDeg < -180.0) lonDeg += 360.0;

    double lat = deg2rad(latDeg);
    double lon = deg2rad(lonDeg);

    double sLat = std::sin(lat), cLat = std::cos(lat);
    double sLon = std::sin(lon), cLon = std::cos(lon);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(sLat) || !std::isfinite(cLat) || 
        !std::isfinite(sLon) || !std::isfinite(cLon)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    // 计算各分量
    double east = -sLon * d[0] + cLon * d[1];
    double north = -sLat * cLon * d[0] - sLat * sLon * d[1] + cLat * d[2];
    double up = cLat * cLon * d[0] + cLat * sLon * d[1] + sLat * d[2];

    // 检查结果的有效性
    if (!std::isfinite(east) || !std::isfinite(north) || !std::isfinite(up)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    return {east, north, up};
}

// ============================================================================
// computeLookAngle — 一站式计算仰角、方位角、斜距
// ============================================================================
LookAngle CoordTransform::computeLookAngle(
    const std::array<double, 3> &satECEF,
    const std::array<double, 3> &staECEF,
    double staLatDeg, double staLonDeg)
{
    // 输入验证（失败返回 NaN: 与 eciToEcef 策略对齐, 调用方
    // elevationAt 通过 !std::isfinite 检查自然拦截）
    if (!std::isfinite(staLatDeg) || !std::isfinite(staLonDeg)) {
        return LookAngle(std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN());
    }

    // 相对矢量
    std::array<double, 3> d = {
        satECEF[0] - staECEF[0],
        satECEF[1] - staECEF[1],
        satECEF[2] - staECEF[2]
    };

    // 检查输入坐标的有效性
    for (const auto& coord : {satECEF, staECEF}) {
        if (!std::isfinite(coord[0]) || !std::isfinite(coord[1]) || !std::isfinite(coord[2])) {
            return LookAngle(std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN());
        }
    }

    auto enu = ecefToEnuComponents(d, staLatDeg, staLonDeg);
    double e = enu[0], n = enu[1], u = enu[2];
    double range = std::sqrt(e * e + n * n + u * u);

    if (range < 1e-9) {
        return LookAngle(90.0, 0.0, 0.0);  // 卫星在头顶（理想情况）
    }

    // 检查ENU分量是否有效
    if (!std::isfinite(e) || !std::isfinite(n) || !std::isfinite(u)) {
        return LookAngle(std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN());
    }

    // 计算归一化的高度分量，并进行严格范围检查
    double u_normalized = u / range;
    
    // 防止浮点误差导致超出[-1, 1]范围
    if (u_normalized < -1.0) u_normalized = -1.0;
    else if (u_normalized > 1.0) u_normalized = 1.0;
    
    // 检查结果是否为有效数值
    if (!std::isfinite(u_normalized)) {
        return LookAngle(std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN());
    }

    double elDeg = std::asin(u_normalized) * 180.0 / PI;
    
    // 计算方位角
    double azDeg = std::atan2(e, n) * 180.0 / PI;    // atan2(E,N): 0°=N, 90°=E
    if (azDeg < 0.0) azDeg += 360.0;

    // 确保结果在有效范围内
    elDeg = std::clamp(elDeg, -90.0, 90.0);
    azDeg = std::fmod(azDeg, 360.0);
    if (azDeg < 0.0) azDeg += 360.0;

    // 几何仰角（detic, 椭球法线）: 与 STK AER 报告同定义——**不含**大气折射。
    // STK 的折射是可选模型（Effective Radius / ITU-R P.834-4 / SCF），
    // 且默认不参与 Access 计算（Help > Sensors > Refraction），本工程一律按
    // 几何仰角口径输出，避免与 STK 报告/约束对不上
    return LookAngle(elDeg, azDeg, range);
}
