// =============================================================================
// Magnitude.cpp — 星等估算实现
// =============================================================================
#include "Magnitude.h"
#include <algorithm>
#include <cctype>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// 常见亮卫星固有星等（1000 km、满相位视角）——近似值，参考 qsmag (Quicksat)
// 与 McCants 数据库；默认(未知)卫星回退值由调用方提供
struct StdMagEntry {
    const char *frag;   // 名称子串（卫星名通常为大写）
    double      mag;    // 固有星等（越小越亮）
};

const StdMagEntry kCatalog[] = {
    { "ISS",           -1.8 },   // 国际空间站
    { "TIANGONG",      -0.9 },   // 天宫空间站
    { "CSS",           -0.9 },   // 天宫空间站（css 名称变体）
    { "HUBBLE",         1.4 },   // 哈勃
    { "ENVISAT",        3.9 },   // 环境卫星（大型）
    { "TERRA",          3.9 },
    { "AQUA",           4.4 },
    { "METOP",          4.0 },
    { "NOAA-15",        4.8 },
    { "NOAA-18",        4.8 },
    { "NOAA-19",        4.8 },
    { "LANDSAT",        5.5 },
    { "SENTINEL",       5.5 },
    { "SUOMI",          4.6 },
    { "STARLINK",       5.0 },
    { "ONEWEB",         5.8 },
    { "GLOBSTAR",       5.8 },
    { "IRIDIUM",        5.6 },
    { "GPS",            5.0 },
    { "BEIDOU",         5.5 },
    { "GLONASS",        5.6 },
    { "GALILEO",        5.8 },
    { "METEOSAT",       6.5 },
    { "GOES",           6.5 },
    { "FENGYUN",        5.9 },
};

} // namespace

double MagnitudeCalc::standardMagnitude(const std::string &satName, double fallback)
{
    if (satName.empty()) return fallback;
    // 大小写不敏感匹配（TLE 名称可能是 "starlink 1234" 等混写变体）
    std::string upper = satName;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (const auto &e : kCatalog) {
        if (upper.find(e.frag) != std::string::npos) return e.mag;
    }
    return fallback;
}

double MagnitudeCalc::phaseAngleRad(const std::array<double, 3> &satEcefKm,
                                    const std::array<double, 3> &sunEcefKm,
                                    const std::array<double, 3> &obsEcefKm)
{
    // 从卫星指向太阳 / 指向观测者的单位矢量
    double vSun[3] = { sunEcefKm[0] - satEcefKm[0],
                       sunEcefKm[1] - satEcefKm[1],
                       sunEcefKm[2] - satEcefKm[2] };
    double vObs[3] = { obsEcefKm[0] - satEcefKm[0],
                       obsEcefKm[1] - satEcefKm[1],
                       obsEcefKm[2] - satEcefKm[2] };
    double nSun = std::sqrt(vSun[0]*vSun[0] + vSun[1]*vSun[1] + vSun[2]*vSun[2]);
    double nObs = std::sqrt(vObs[0]*vObs[0] + vObs[1]*vObs[1] + vObs[2]*vObs[2]);
    if (nSun <= 0.0 || nObs <= 0.0) return M_PI;   // 退化几何: 按最暗处理
    double dot = (vSun[0]*vObs[0] + vSun[1]*vObs[1] + vSun[2]*vObs[2]) / (nSun * nObs);
    if (dot >  1.0) dot =  1.0;
    if (dot < -1.0) dot = -1.0;
    return std::acos(dot);
}

double MagnitudeCalc::visualMagnitude(double stdMag, double rangeKm, double phaseRad)
{
    // Vallerie 1963: m = m₀ − 15 + 5·log10(R) − 2.5·log10(f(B))
    // f(B) = (sin B + (π − B)·cos B)/π: B=0 → 1（满相位），B=π → 0（背日）
    double B = phaseRad;
    if (B < 0.0) B = 0.0;
    if (B > M_PI) B = M_PI;
    double f = (std::sin(B) + (M_PI - B) * std::cos(B)) / M_PI;
    if (f < 1e-6) f = 1e-6;   // 防 log10(0) → −∞（星等 +∞）
    return stdMag - 15.0 + 5.0 * std::log10(rangeKm) - 2.5 * std::log10(f);
}