// =============================================================================
// CoordTransform.cpp — 坐标转换完整实现
// =============================================================================
#include "CoordTransform.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
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
    
    // GMST 缓存结构
    struct GmstCacheEntry {
        double jdUt1;
        double gmstRad;
        
        GmstCacheEntry(double jd, double gmst) : jdUt1(jd), gmstRad(gmst) {}
        
        bool isClose(double jd, double tolerance = 1e-8) const {
            return std::abs(jdUt1 - jd) < tolerance;
        }
    };
    
    // GMST 缓存
    static std::vector<GmstCacheEntry> gmstCache;
    
    // 清除 GMST 缓存
    void clearGmstCache() {
        gmstCache.clear();
    }
    
    // 添加 GMST 缓存项
    void addGmstCache(double jdUt1, double gmstRad) {
        if (gmstCache.size() >= CoordTransform::MAX_CACHE_SIZE) {
            // 如果缓存满了，移除最旧的项
            gmstCache.erase(gmstCache.begin());
        }
        gmstCache.emplace_back(jdUt1, gmstRad);
    }
    
    // 从缓存查找 GMST
    std::optional<double> findGmstCache(double jdUt1) {
        for (const auto& entry : gmstCache) {
            if (entry.isClose(jdUt1)) {
                return entry.gmstRad;
            }
        }
        return std::nullopt;
    }
}

// ============================================================================
// gmstRad — IAU-1982 格林尼治平恒星时（弧度）
// 输入为 UT1 儒略日；可用 UTC 直接近似（UT1−UTC ≤ 0.9s），
// 切勿加 ΔT（ΔT = TT−UT1，加了得到 TT，地球自转角会系统性偏大 ~69s 恒星时）
// ============================================================================
double CoordTransform::gmstRad(double jdUt1)
{
    try {
        // 输入验证
        if (!std::isfinite(jdUt1)) {
            throw std::invalid_argument("无效的儒略日输入");
        }

        // 优化：检查缓存
        auto cached = findGmstCache(jdUt1);
        if (cached) {
            return *cached;
        }

        // 当天 0h UT1 的儒略日
        double jd0 = std::floor(jdUt1 - 0.5) + 0.5;

        // J2000.0 起算的儒略世纪
        double T = (jd0 - 2451545.0) / 36525.0;
        
        // 检查T的有效性
        if (!std::isfinite(T)) {
            throw std::invalid_argument("儒略世纪计算结果无效");
        }

        // 当天 0h 的 GMST（秒）
        double gmstSec = 24110.54841
                       + 8640184.812866 * T
                       + 0.093104 * T * T
                       - 6.2e-6 * T * T * T;
        
        // 检查gmstSec的有效性
        if (!std::isfinite(gmstSec)) {
            throw std::invalid_argument("GMST秒数计算结果无效");
        }

        // 加上当天 UT1 小数的贡献（恒星日 / 太阳日 ≈ 1.00273790935）
        double fracDay = (jdUt1 - jd0);
        gmstSec += SEC_PER_DAY * fracDay * 1.00273790935;
        
        // 检查fracDay的有效性
        if (!std::isfinite(fracDay)) {
            throw std::invalid_argument("儒略日小数部分计算结果无效");
        }

        // 取模 → 转为弧度 → 正规化到 [0, 2π)
        gmstSec = std::fmod(gmstSec, SEC_PER_DAY);
        if (gmstSec < 0.0) gmstSec += SEC_PER_DAY;

        double gmstRad = gmstSec * TWOPI / SEC_PER_DAY;
        
        // 确保结果在有效范围内
        if (!std::isfinite(gmstRad)) {
            throw std::invalid_argument("GMST弧度计算结果无效");
        }
        
        // 规范化到[0, 2π)范围
        gmstRad = std::fmod(gmstRad, TWOPI);
        if (gmstRad < 0.0) gmstRad += TWOPI;

        // 优化：添加到缓存
        addGmstCache(jdUt1, gmstRad);

        return gmstRad;
    } catch (const std::exception &e) {
        // 记录错误信息（在实际应用中可以使用日志系统）
        // std::cerr << "gmstRad 计算失败: " << e.what() << std::endl;
        return 0.0;  // 返回零值作为错误处理
    }
}

// ============================================================================
// deltaTSeconds — ΔT = TT − UT1（秒）
// Espenak & Meeus (2006) 分段多项式, 覆盖 1900~2100 年
// 应用内历元范围之外时取边界值，保证连续
// ============================================================================
double CoordTransform::deltaTSeconds(double jdUtc)
{
    // 输入验证
    if (!std::isfinite(jdUtc)) {
        return 0.0;  // 无效输入返回零值
    }

    // 儒略日 → 年份（近似，足够 ΔT 使用）
    double year = 2000.0 + (jdUtc - 2451545.0) / 365.2425;
    
    // 检查年份的有效性
    if (!std::isfinite(year)) {
        return 0.0;  // 无效年份返回零值
    }

    if (year < 1900.0) {
        // 取 1900 年处边界值（t = y − 1900 = 0），保证连续
        return -2.79;
    }
    if (year < 1920.0) {
        double t = (year - 1900.0) / 100.0;
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
    // ≥ 2050（含 extrapolate 2100 后）
    double t = (year - 1820.0) / 100.0;
    if (!std::isfinite(t)) return -20.0;  // 无效t值返回边界值
    double term1 = -20.0 + 32.0 * t * t;
    double term2 = -0.5628 * (2150.0 - year);
    if (!std::isfinite(term2)) return term1;  // 无效项返回部分结果
    return term1 + term2;
}

// ============================================================================
// eciToEcef — TEME → ECEF（绕 Z 轴旋转 GMST 角）
// ============================================================================
std::array<double, 3> CoordTransform::eciToEcef(
    const std::array<double, 3> &posTeme, double jdUt1)
{
    try {
        // 输入验证
        if (!std::isfinite(jdUt1)) {
            throw std::invalid_argument("无效的儒略日输入");
        }
        
        // 检查位置矢量
        if (!std::isfinite(posTeme[0]) || !std::isfinite(posTeme[1]) || !std::isfinite(posTeme[2])) {
            throw std::invalid_argument("无效的位置矢量输入");
        }

        double theta = gmstRad(jdUt1);
        if (!std::isfinite(theta)) {
            throw std::invalid_argument("GMST角度计算结果无效");
        }
        
        double c = std::cos(theta);
        double s = std::sin(theta);
        
        // 检查三角函数值的有效性
        if (!std::isfinite(c) || !std::isfinite(s)) {
            throw std::invalid_argument("三角函数计算结果无效");
        }

        double x = posTeme[0] * c + posTeme[1] * s;
        double y = -posTeme[0] * s + posTeme[1] * c;
        double z = posTeme[2];

        // 检查结果的有效性
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            throw std::invalid_argument("坐标转换结果无效");
        }

        return {x, y, z};
    } catch (const std::exception &e) {
        // 记录错误信息（在实际应用中可以使用日志系统）
        // std::cerr << "eciToEcef 转换失败: " << e.what() << std::endl;
        return {0.0, 0.0, 0.0};  // 返回零值作为错误处理
    }
}

// ============================================================================
// eciDirToEcef — 方向单位矢量 ECI → ECEF（仅旋转 GMST）
// ============================================================================
std::array<double, 3> CoordTransform::eciDirToEcef(
    const std::array<double, 3> &dirTeme, double jdUt1)
{
    // 输入验证
    if (!std::isfinite(jdUt1)) {
        return {0.0, 0.0, 0.0};  // 无效输入返回零值
    }
    
    // 检查方向矢量
    if (!std::isfinite(dirTeme[0]) || !std::isfinite(dirTeme[1]) || !std::isfinite(dirTeme[2])) {
        return {0.0, 0.0, 0.0};  // 无效方向返回零值
    }

    double theta = gmstRad(jdUt1);
    if (!std::isfinite(theta)) {
        return {0.0, 0.0, 0.0};  // 无效角度返回零值
    }
    
    double c = std::cos(theta);
    double s = std::sin(theta);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(c) || !std::isfinite(s)) {
        return {0.0, 0.0, 0.0};  // 无效三角函数返回零值
    }

    double x = dirTeme[0] * c + dirTeme[1] * s;
    double y = -dirTeme[0] * s + dirTeme[1] * c;
    double z = dirTeme[2];

    // 检查结果的有效性
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return {0.0, 0.0, 0.0};  // 无效结果返回零值
    }

    return {x, y, z};
}

// ============================================================================
// eciStateToEcef — TEME 位置+速度 → ECEF（含地球自转哥里奥利项）
// ============================================================================
std::pair<std::array<double, 3>, std::array<double, 3>>
CoordTransform::eciStateToEcef(const std::array<double, 3> &posTeme,
                               const std::array<double, 3> &velTeme,
                               double jdUt1)
{
    // 输入验证
    if (!std::isfinite(jdUt1)) {
        return {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};  // 无效输入返回零值
    }
    
    // 检查位置和速度矢量
    if (!std::isfinite(posTeme[0]) || !std::isfinite(posTeme[1]) || !std::isfinite(posTeme[2]) ||
        !std::isfinite(velTeme[0]) || !std::isfinite(velTeme[1]) || !std::isfinite(velTeme[2])) {
        return {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};  // 无效矢量返回零值
    }

    double theta = gmstRad(jdUt1);
    if (!std::isfinite(theta)) {
        return {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};  // 无效角度返回零值
    }
    
    double c = std::cos(theta);
    double s = std::sin(theta);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(c) || !std::isfinite(s)) {
        return {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};  // 无效三角函数返回零值
    }

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
    
    // 检查结果的有效性
    for (const auto& vec : {posEcef, velEcef}) {
        if (!std::isfinite(vec[0]) || !std::isfinite(vec[1]) || !std::isfinite(vec[2])) {
            return {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};  // 无效结果返回零值
        }
    }

    return { posEcef, velEcef };
}

// ============================================================================
// ecefToGeodetic — Bowring 1976 闭式解法: ECEF → WGS84 经纬高
// ============================================================================
GeoPos CoordTransform::ecefToGeodetic(const std::array<double, 3> &p)
{
    double x = p[0], y = p[1], z = p[2];
    
    // 输入验证
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return GeoPos(0.0, 0.0, 0.0);  // 无效输入返回零值
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

    // 防止分母为零
    if (r < 1e-9) {
        r = 1e-9;
    }

    double theta = std::atan2(z * a, r * b);
    double sinT = std::sin(theta), cosT = std::cos(theta);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(sinT) || !std::isfinite(cosT)) {
        return GeoPos(0.0, lonDeg, 0.0);  // 无效计算返回零值
    }

    double lat = std::atan2(z + ep2 * b * sinT * sinT * sinT,
                            r - e2 * a * cosT * cosT * cosT);
    
    // 检查纬度计算结果
    if (!std::isfinite(lat)) {
        return GeoPos(0.0, lonDeg, 0.0);  // 无效纬度返回零值
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
        return {0.0, 0.0, 0.0};  // 无效输入返回零值
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
        return {0.0, 0.0, 0.0};  // 无效三角函数返回零值
    }

    // 计算卯酉圈曲率半径N，避免数值不稳定
    double e2_sin2 = WGS84_E2 * sinLat * sinLat;
    if (e2_sin2 >= 1.0) {
        // 纬度过大导致数值不稳定，使用边界值
        return {0.0, 0.0, 0.0};
    }
    
    double N = WGS84_A_KM / std::sqrt(1.0 - e2_sin2);
    
    // 检查N的有效性
    if (!std::isfinite(N) || N <= 0.0) {
        return {0.0, 0.0, 0.0};  // 无效N值返回零值
    }

    // 计算各分量
    double cosLon = std::cos(lon);
    double sinLon = std::sin(lon);
    
    // 检查经度三角函数值的有效性
    if (!std::isfinite(cosLon) || !std::isfinite(sinLon)) {
        return {0.0, 0.0, 0.0};  // 无效三角函数返回零值
    }

    double N_plus_alt = N + alt;
    double x = N_plus_alt * cosLat * cosLon;
    double y = N_plus_alt * cosLat * sinLon;
    double z = ((1.0 - WGS84_E2) * N + alt) * sinLat;

    // 检查最终结果的有效性
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return {0.0, 0.0, 0.0};  // 无效结果返回零值
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
        return {0.0, 0.0, 0.0};  // 无效输入返回零值
    }
    
    // 检查相对矢量
    if (!std::isfinite(d[0]) || !std::isfinite(d[1]) || !std::isfinite(d[2])) {
        return {0.0, 0.0, 0.0};  // 无效矢量返回零值
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
        return {0.0, 0.0, 0.0};  // 无效三角函数返回零值
    }

    // 计算各分量
    double east = -sLon * d[0] + cLon * d[1];
    double north = -sLat * cLon * d[0] - sLat * sLon * d[1] + cLat * d[2];
    double up = cLat * cLon * d[0] + cLat * sLon * d[1] + sLat * d[2];

    // 检查结果的有效性
    if (!std::isfinite(east) || !std::isfinite(north) || !std::isfinite(up)) {
        return {0.0, 0.0, 0.0};  // 无效结果返回零值
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
    // 输入验证
    if (!std::isfinite(staLatDeg) || !std::isfinite(staLonDeg)) {
        return LookAngle(0.0, 0.0, 0.0);  // 无效输入返回零值
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
            return LookAngle(0.0, 0.0, 0.0);  // 无效坐标返回零值
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
        return LookAngle(0.0, 0.0, 0.0);  // 无效ENU分量返回零值
    }

    // 计算归一化的高度分量，并进行严格范围检查
    double u_normalized = u / range;
    
    // 防止浮点误差导致超出[-1, 1]范围
    if (u_normalized < -1.0) u_normalized = -1.0;
    else if (u_normalized > 1.0) u_normalized = 1.0;
    
    // 检查结果是否为有效数值
    if (!std::isfinite(u_normalized)) {
        return LookAngle(0.0, 0.0, 0.0);
    }

    double elDeg = std::asin(u_normalized) * 180.0 / PI;
    
    // 计算方位角
    double azDeg = std::atan2(e, n) * 180.0 / PI;    // atan2(E,N): 0°=N, 90°=E
    if (azDeg < 0.0) azDeg += 360.0;

    // 确保结果在有效范围内
    elDeg = std::clamp(elDeg, -90.0, 90.0);
    azDeg = std::fmod(azDeg, 360.0);
    if (azDeg < 0.0) azDeg += 360.0;

    return LookAngle(elDeg, azDeg, range);
}

// ============================================================================
// applyRefraction — 大气折射修正（Bennett 1982）
// ============================================================================
double CoordTransform::applyRefraction(double elGeomDeg)
{
    // 输入验证
    if (!std::isfinite(elGeomDeg)) {
        return elGeomDeg;  // 无效输入直接返回
    }

    // Bennett 公式在负仰角下不适用（tan 会变号）
    if (elGeomDeg < 0.0) return elGeomDeg;
    
    // 极高仰角（近天顶）时 Bennett 公式分子趋 0 甚至变负，
    // 折射修正物理上必然非负 → 直接返回原值（L-3 修复）
    if (elGeomDeg >= 88.0) return elGeomDeg;

    // 检查分母是否接近零点，避免数值不稳定
    double denominator = elGeomDeg + 4.4;
    if (std::fabs(denominator) < 1e-10) {
        // 接近奇点，使用线性外推
        return elGeomDeg;
    }

    // Bennett 1982: R(弧分) = 1 / tan(h + 7.31/(h + 4.4))
    // h = 仰角（度）
    double h_deg = elGeomDeg + 7.31 / denominator;
    double hRad = deg2rad(h_deg);
    
    // 检查hRad是否在有效范围内
    if (!std::isfinite(hRad) || hRad < 0.0 || hRad > PI) {
        return elGeomDeg;  // 无效计算直接返回原值
    }

    double tan_h = std::tan(hRad);
    
    // 检查tan值是否过大或过小
    if (std::fabs(tan_h) < 1e-12) {
        // 接近π/2，使用渐进行为
        return elGeomDeg + 0.0;  // 天顶附近折射趋近于0
    }
    
    double R_arcmin = 1.0 / tan_h;
    
    // 不使用简单的钳制为0，而是检查数值问题的根源
    if (R_arcmin < 0.0) {
        // 负值可能是由于数值误差或物理上不可能的情况
        // 记录错误并使用保守估计
        R_arcmin = 0.0;
    }

    // 检查折射值是否合理（物理上折射角通常小于1度）
    if (R_arcmin > 60.0) {  // 大于1度
        R_arcmin = 60.0;  // 钳制到最大合理值
    }

    // [0,1]° 线性过渡带: Bennett 在 0° 处 R≈0.57° 的突跳会让
    // 掩星边缘的"视仰角"不连续 → 按 el/1° 线性缩放，从 0 平滑过渡
    // 到 el=1° 处的 Bennett 值（el=1 时系数为 1，与 Bennett 无缝衔接；
    // Bennett 从 1° 到 0° 仅从 0.41° 升至 0.57°，线性缩放后全程单调）
    if (elGeomDeg < 1.0) {
        R_arcmin *= elGeomDeg;
    }

    double result = elGeomDeg + R_arcmin / 60.0;  // 弧分 → 度
    
    // 确保结果有效且在合理范围内
    if (!std::isfinite(result) || result < elGeomDeg) {
        return elGeomDeg;  // 无效结果返回原值
    }

    return result;
}

// ============================================================================
// 缓存管理函数
// ============================================================================
void CoordTransform::clearCache() {
    clearGmstCache();
}

size_t CoordTransform::getCacheSize() {
    return gmstCache.size();
}
