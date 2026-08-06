// =============================================================================
// SunPosition.cpp — Meeus "Astronomical Algorithms" 第25章低精度太阳星历
// =============================================================================
#include "SunPosition.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
    constexpr double PI     = M_PI;
    constexpr double TWOPI  = 2.0 * PI;
    constexpr double DEG2RAD = PI / 180.0;
    constexpr double RAD2DEG = 180.0 / PI;

    inline double deg2rad(double d) { return d * DEG2RAD; }
    inline double rad2deg(double r) { return r * RAD2DEG; }

    // 正规化角度到 [0, 2π)
    double wrap2Pi(double ang) {
        ang = std::fmod(ang, TWOPI);
        if (ang < 0.0) ang += TWOPI;
        return ang;
    }

    // GMST（IAU-1982 简化版，弧度）
    double gmst(double jd) {
        double jd0 = std::floor(jd - 0.5) + 0.5;
        double T   = (jd0 - 2451545.0) / 36525.0;
        double gmstSec = 24110.54841
                       + 8640184.812866 * T
                       + 0.093104 * T * T - 6.2e-6 * T * T * T;
        double fracDay = (jd - jd0);
        gmstSec += 86400.0 * fracDay * 1.00273790935;
        gmstSec = std::fmod(gmstSec, 86400.0);
        if (gmstSec < 0.0) gmstSec += 86400.0;
        return gmstSec * TWOPI / 86400.0;
    }
}

SunPosition SunPositionCalc::compute(const double jd,
                                     double obsLatDeg, double obsLonDeg)
{
    const double obsLat = deg2rad(obsLatDeg);
    const double obsLon = deg2rad(obsLonDeg);
    SunPosition sp;

    // ---- Step 1: 儒略千年数（J2000.0 起算） ----
    double T = (jd - 2451545.0) / 365250.0;

    // ---- Step 2: 太阳平黄经（度 → rad） ----
    double L0 = deg2rad(280.46646 + 36000.76983 * (T * 10.0)
                        + 0.0003032 * (T * 10.0) * (T * 10.0));
    L0 = wrap2Pi(L0);

    // ---- Step 3: 太阳平近点角 ----
    double M = deg2rad(357.52911 + 35999.05029 * (T * 10.0)
                       - 0.0001537 * (T * 10.0) * (T * 10.0));
    M = wrap2Pi(M);

    // ---- Step 4: 中心差 ----
    double C = deg2rad((1.914602 - 0.004817 * (T * 10.0) - 0.000014 * (T * 10.0) * (T * 10.0)) * std::sin(M)
                     + (0.019993 - 0.000101 * (T * 10.0)) * std::sin(2.0 * M)
                     + 0.000289 * std::sin(3.0 * M));

    // ---- Step 5: 太阳视黄经 λ ----
    // 加上光行差与黄经章动主项修正 (Meeus 25.9):
    //   Δλ = −0.00569° − 0.00478°·sin(Ω)，Ω = 125.04° − 1934.136°·T(儒略世纪)
    // 使 RA/Dec 从几何位置提升到视位置（精度 ≈ 0.01° 量级）
    double lambda = wrap2Pi(L0 + C);
    double omega  = deg2rad(125.04 - 1934.136 * (T * 10.0));
    lambda = wrap2Pi(lambda + deg2rad(-0.00569 - 0.00478 * std::sin(omega)));

    // ---- Step 6: 黄赤交角 ε ----
    double eps = deg2rad(23.43929111
                         - 0.01300416667 * (T * 10.0)
                         - 1.6389e-7 * (T * 10.0) * (T * 10.0)
                         + 5.0361e-7 * (T * 10.0) * (T * 10.0) * (T * 10.0));

    // ---- Step 7: 黄道 → 赤道 (RA, Dec) ----
    double sinLambda = std::sin(lambda);
    double cosLambda = std::cos(lambda);
    double sinEps    = std::sin(eps);
    double cosEps    = std::cos(eps);

    double sinDec = sinEps * sinLambda;
    double cosDec = std::sqrt(1.0 - sinDec * sinDec);  // Dec 在 ±23.5° 内，正值
    double dec    = std::atan2(sinDec, cosDec);

    double sinRA  = sinLambda * cosEps;  // 符号同 sinLambda
    double cosRA  = cosLambda;
    double ra     = std::atan2(sinRA, cosRA);
    ra = wrap2Pi(ra);

    sp.raDeg  = rad2deg(ra);
    sp.decDeg = rad2deg(dec);

    // ---- Step 8: 日地距离（AU） ----
    sp.distanceAU = 1.00014 - 0.01671 * std::cos(M) - 0.00014 * std::cos(2.0 * M);

    // ---- Step 9: 太阳地心方向单位矢量（ECI J2000） ----
    sp.dirECI = {
        std::cos(dec) * std::cos(ra),
        std::cos(dec) * std::sin(ra),
        std::sin(dec)
    };

    // ---- Step 10: 赤道 → 地平（观测站） ----
    double gmstRad = gmst(jd);
    double lha = gmstRad + obsLon - ra;  // 地方时角
    lha = wrap2Pi(lha);

    double sinAlt = std::sin(obsLat) * std::sin(dec)
                  + std::cos(obsLat) * std::cos(dec) * std::cos(lha);
    double alt = std::asin(sinAlt);

    double sinAz = -std::sin(lha) * std::cos(dec) / std::cos(alt);
    double cosAz = (std::sin(dec) - std::sin(obsLat) * std::sin(alt))
                  / (std::cos(obsLat) * std::cos(alt));
    double az = std::atan2(sinAz, cosAz);
    az = wrap2Pi(az);

    sp.azDeg  = rad2deg(az);     // 北=0°, 顺时针
    sp.altDeg = rad2deg(alt);    // 地平=0°

    return sp;
}
