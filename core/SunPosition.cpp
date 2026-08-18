// =============================================================================
// SunPosition.cpp — Meeus "Astronomical Algorithms" 第25章低精度太阳星历
// =============================================================================
#include "SunPosition.h"
#include "CoordTransform.h"
#include "Nutation.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
    constexpr double PI     = M_PI;
    constexpr double TWOPI  = 2.0 * PI;
    constexpr double DEG2RAD = PI / 180.0;
    constexpr double RAD2DEG = 180.0 / PI;
    constexpr double SEC_PER_DAY = 86400.0;

    inline double deg2rad(double d) { return d * DEG2RAD; }
    inline double rad2deg(double r) { return r * RAD2DEG; }

    // 正规化角度到 [0, 2π)
    double wrap2Pi(double ang) {
        ang = std::fmod(ang, TWOPI);
        if (ang < 0.0) ang += TWOPI;
        return ang;
    }
}

std::optional<SunPosition> SunPositionCalc::compute(const double jdUtc,
                                                    double obsLatDeg, double obsLonDeg)
{
    try {
        // 输入验证
        if (!std::isfinite(jdUtc) || !std::isfinite(obsLatDeg) || !std::isfinite(obsLonDeg)) {
            throw std::invalid_argument("无效的输入参数");
        }

        // UTC → TT（历表使用）：ΔT 秒级修正，远小于公式自身精度
        const double jdTT = jdUtc + CoordTransform::deltaTSeconds(jdUtc) / SEC_PER_DAY;
        if (!std::isfinite(jdTT)) {
            throw std::invalid_argument("TT时间计算结果无效");
        }

        const double obsLat = deg2rad(obsLatDeg);
        const double obsLon = deg2rad(obsLonDeg);
        SunPosition sp;

        // ---- Step 1: 儒略千年数（J2000.0 起算, TT 时标） ----
        // 注: T 以千年为单位(除数 365250), 而 Meeus 系数按世纪编制,
        // 后续多项式须以 (T×10) 折算——勿被注释误导改回世纪制
        double T = (jdTT - 2451545.0) / 365250.0;
        if (!std::isfinite(T)) {
            throw std::invalid_argument("儒略千年数计算结果无效");
        }

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
        // 几何黄经 λ_geo = L0 + C（平均轨道项）
        double lambda = wrap2Pi(L0 + C);

        // 光行差 −20.4898″/R（Meeus 25.9，R 为日地距离 AU，见 Step 8）
        // 章动 Δψ（Meeus 表 22.A 完整 63 项级数，精度 0.5″）
        const double distAU = 1.00014 - 0.01671 * std::cos(M)
                            - 0.00014 * std::cos(2.0 * M);
        if (!std::isfinite(distAU) || distAU <= 0.0) {
            throw std::invalid_argument("日地距离计算结果无效");
        }
        
        const double dPsi = Nutation::compute(jdTT).dPsi;
        lambda = wrap2Pi(lambda + dPsi - deg2rad(20.4898 / 3600.0 / distAU));

        // ---- Step 6: 真黄赤交角 ε = ε0 + Δε ----
        double eps = deg2rad(Nutation::trueObliquityDeg(jdTT));
        if (!std::isfinite(eps)) {
            throw std::invalid_argument("黄赤交角计算结果无效");
        }

        // ---- Step 7: 黄道 → 赤道 (RA, Dec) ----
        double sinLambda = std::sin(lambda);
        double cosLambda = std::cos(lambda);
        double sinEps    = std::sin(eps);
        double cosEps    = std::cos(eps);

        if (!std::isfinite(sinLambda) || !std::isfinite(cosLambda) || 
            !std::isfinite(sinEps) || !std::isfinite(cosEps)) {
            throw std::invalid_argument("三角函数计算结果无效");
        }

        double sinDec = sinEps * sinLambda;
        double cosDec = std::sqrt(std::max(0.0, 1.0 - sinDec * sinDec));  // Dec 在 ±23.5° 内，正值
        double dec    = std::atan2(sinDec, cosDec);

        double sinRA  = sinLambda * cosEps;  // 符号同 sinLambda
        double cosRA  = cosLambda;
        double ra     = std::atan2(sinRA, cosRA);
        ra = wrap2Pi(ra);

        sp.raDeg  = rad2deg(ra);
        sp.decDeg = rad2deg(dec);

        // ---- Step 8: 日地距离（AU） ----
        sp.distanceAU = distAU;

        // ---- Step 9: 太阳地心方向单位矢量（ECI J2000） ----
        sp.dirECI = {
            std::cos(dec) * std::cos(ra),
            std::cos(dec) * std::sin(ra),
            std::sin(dec)
        };

        // ---- Step 10: 赤道 → 地平（观测站） ----
        // 时角用 UT1 时标的 GMST；UT1 ≈ UTC（≤ 0.9s），直接用 UTC 儒略日，
        // 不再加 ΔT（ΔT 仅用于上方 TT 时标的太阳历表）
        double gmstRad = CoordTransform::gmstRad(jdUtc);
        double lha = gmstRad + obsLon - ra;  // 地方时角
        lha = wrap2Pi(lha);

        double sinAlt = std::sin(obsLat) * std::sin(dec)
                      + std::cos(obsLat) * std::cos(dec) * std::cos(lha);
        // 钳制防 asin 越界（浮点舍入可致 |sinAlt| 略超 1）
        sinAlt = std::min(1.0, std::max(-1.0, sinAlt));
        double alt = std::asin(sinAlt);

        // 防除零: cos(alt)→0（太阳在天顶）与 cos(lat)→0（站在极点）时
        // 方位角无定义，返回 0（北），避免 0/0 → NaN
        const double cosAlt = std::max(1e-12, std::cos(alt));
        const double cosLat = std::max(1e-12, std::cos(obsLat));
        double sinAz = -std::sin(lha) * std::cos(dec) / cosAlt;
        double cosAz = (std::sin(dec) - std::sin(obsLat) * std::sin(alt))
                      / (cosLat * cosAlt);
        double az = std::atan2(sinAz, cosAz);
        az = wrap2Pi(az);

        // 方位角在两类退化位置下无物理意义且数值会任意跳变:
        // 1) 太阳在天顶/天底 (|alt|≈±90°)  2) 观测站在极点 (|lat|≈±90°)
        // → 稳定钳制为 0°（北），避免极区时段内方位角无意义的抖动
        const double absAltDeg  = std::abs(rad2deg(alt));
        const double absLatDeg  = std::abs(rad2deg(obsLat));
        if (absAltDeg >= 89.9 || absLatDeg >= 89.9) az = 0.0;

        sp.azDeg  = rad2deg(az);     // 北=0°, 顺时针
        sp.altDeg = rad2deg(alt);    // 地平=0°

        // 最终结果验证
        if (!std::isfinite(sp.raDeg) || !std::isfinite(sp.decDeg) || 
            !std::isfinite(sp.azDeg) || !std::isfinite(sp.altDeg) ||
            !std::isfinite(sp.distanceAU)) {
            throw std::invalid_argument("太阳位置计算结果无效");
        }

        return sp;
    } catch (const std::exception &e) {
        // 计算失败（输入非法/中间值非有限）: 不吞错返回默认值——
        // 调用侧收到 nullopt 可区分"真失败"与"合法结果"，避免把
        // RA=0/Dec=0 的伪结果当真实数据使用
        return std::nullopt;
    }
}
