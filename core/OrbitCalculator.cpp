// =============================================================================
// OrbitCalculator.cpp — 过境预报引擎完整实现
// =============================================================================
#include "OrbitCalculator.h"
#include "CoordTransform.h"
#include "SunPosition.h"
#include "EarthShadow.h"
#include "DopplerCalc.h"
#include <cmath>
#include <algorithm>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
    constexpr double MU_KM3_S2   = 398600.8;   // 地球引力常数 km³/s²
    constexpr double EXTEND_MIN   = 60.0;      // 跨窗口过境延长搜索（分钟）
    constexpr double TCA_REFINE_SEC = 3.0;     // TCA 三分搜索初始半宽（秒）
    constexpr double SEC_PER_DAY  = 86400.0;

    // UTC 儒略日 → UT1 儒略日（GMST/地固坐标旋转需要 UT1 时标）
    inline double ut1JD(double jdUtc) {
        return jdUtc + CoordTransform::deltaTSeconds(jdUtc) / SEC_PER_DAY;
    }
}

// ========== 辅助：填充一次过境的完整详情 ==========
static void fillPassDetails(PassEvent &ev,
    OrbitCalculator::PropagateFn &propagate, const Observer &observer,
    const OrbitCalculator::Config &cfg,
    double jdAOS, double jdTCA, double jdLOS)
{
    // 光照分析
    if (cfg.enableSunAnalysis) {
        auto sunA = SunPositionCalc::compute(jdAOS, observer.latDeg(), observer.lonDeg());
        auto sunT = SunPositionCalc::compute(jdTCA, observer.latDeg(), observer.lonDeg());
        auto sunL = SunPositionCalc::compute(jdLOS, observer.latDeg(), observer.lonDeg());
        ev.aosSunAlt = sunA.altDeg;
        ev.tcaSunAlt = sunT.altDeg;
        ev.losSunAlt = sunL.altDeg;

        if (cfg.enableShadowCheck) {
            auto eciA = propagate(jdAOS);
            auto eciT = propagate(jdTCA);
            auto eciL = propagate(jdLOS);
            if (eciA) {
                auto eA = CoordTransform::eciToEcef(eciA->posKm, ut1JD(jdAOS));
                // 太阳方向必须旋转到 ECEF 与卫星位置同系
                auto sunDirA = CoordTransform::eciDirToEcef(sunA.dirECI, ut1JD(jdAOS));
                ev.aosSatShadow = EarthShadow::checkShadow(eA, sunDirA, sunA.distanceAU);
                ev.aosIllum = EarthShadow::umbraFactor(eA, sunDirA, sunA.distanceAU);
            }
            if (eciT) {
                auto eT = CoordTransform::eciToEcef(eciT->posKm, ut1JD(jdTCA));
                auto sunDirT = CoordTransform::eciDirToEcef(sunT.dirECI, ut1JD(jdTCA));
                ev.tcaSatShadow = EarthShadow::checkShadow(eT, sunDirT, sunT.distanceAU);
                ev.tcaIllum = EarthShadow::umbraFactor(eT, sunDirT, sunT.distanceAU);
            }
            if (eciL) {
                auto eL = CoordTransform::eciToEcef(eciL->posKm, ut1JD(jdLOS));
                auto sunDirL = CoordTransform::eciDirToEcef(sunL.dirECI, ut1JD(jdLOS));
                ev.losSatShadow = EarthShadow::checkShadow(eL, sunDirL, sunL.distanceAU);
                ev.losIllum = EarthShadow::umbraFactor(eL, sunDirL, sunL.distanceAU);
            }
        }

        double bestSunAlt = std::max({ev.aosSunAlt, ev.tcaSunAlt, ev.losSunAlt});
        // 光照判定以 TCA（用户实际观测时刻）为准 —— 与 Heavens-Above/Orbis
        // 的「峰值点燃亮」语义一致：仅 AOS/LOS 照亮、TCA 却在地影内的过境
        // 不标为可见（否则用户按 TCA 时刻观测看不到卫星）
        bool satLit = (ev.tcaSatShadow != ShadowStatus::InUmbra) && ev.tcaIllum > 0.5;
        if (bestSunAlt < cfg.darkSkyThresholdDeg && satLit)
            ev.visibility = PassVisibility::Visible;
        else if (bestSunAlt < cfg.darkSkyThresholdDeg)
            ev.visibility = PassVisibility::RadioOnly;
        else
            ev.visibility = PassVisibility::Daylight;
    }
    if (cfg.enableRefraction) {
        ev.aosElRefracted = CoordTransform::applyRefraction(ev.aosLook.elevDeg);
        ev.tcaElRefracted = CoordTransform::applyRefraction(ev.tcaLook.elevDeg);
        ev.losElRefracted = CoordTransform::applyRefraction(ev.losLook.elevDeg);
    }
    if (cfg.enableDoppler) {
        auto eciT = propagate(jdTCA);
        if (eciT) {
            ev.dopplerAtTcaKHz = DopplerCalc::compute(
                eciT->posKm, eciT->velKms, jdTCA,
                observer.ecef(), cfg.carrierFreqMHz);
        }
    }
    // 过境类型
    double azDiff = ev.tcaLook.azDeg - ev.aosLook.azDeg;
    if (azDiff > 180.0) azDiff -= 360.0;
    if (azDiff < -180.0) azDiff += 360.0;
    ev.passType = (azDiff > 0.0) ? 0 : 1;
    // 质量等级
    if (ev.maxElevDeg >= 50.0)      ev.quality = PassQuality::Excellent;
    else if (ev.maxElevDeg >= 20.0) ev.quality = PassQuality::Good;
    else                            ev.quality = PassQuality::Poor;
}

int64_t OrbitCalculator::jdToIntMs(double jd) {
    return static_cast<int64_t>(jd * 86400000.0);
}
double OrbitCalculator::intMsToJd(int64_t ms) {
    return static_cast<double>(ms) / 86400000.0;
}

// ========== 单点仰角计算 ==========
double OrbitCalculator::elevationAt(PropagateFn &propagate,
                                     const Observer &obs, double jd) const
{
    auto eciOpt = propagate(jd);
    if (!eciOpt) return -1000.0;  // 传播失败 → 视为不可见

    auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, ut1JD(jd));
    return obs.lookAngle(ecef).elevDeg;
}

// ========== 计算某一时刻的完整观测 + 星下点 ==========
void OrbitCalculator::computeMoment(PropagateFn &propagate,
                                     const Observer &obs, double jd,
                                     LookAngle &look, GeoPos &geo) const
{
    auto eciOpt = propagate(jd);
    if (!eciOpt) {
        look = LookAngle(-1000.0, 0.0, 0.0);
        geo  = GeoPos(0.0, 0.0, 0.0);
        return;
    }
    auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, ut1JD(jd));
    look = obs.lookAngle(ecef);
    geo  = obs.subSatPoint(ecef);
}

// ========== 过境弧段采样 ==========
void OrbitCalculator::collectSamples(PropagateFn &propagate,
                                      const Observer &obs,
                                      double jdAOS, double jdLOS,
                                      PassEvent &ev) const
{
    if (!m_cfg.enableSamples || m_cfg.sampleStepSec <= 0.0) return;
    const double step = m_cfg.sampleStepSec / 86400.0;
    ev.samples.clear();
    ev.samples.reserve(static_cast<size_t>((jdLOS - jdAOS) / step) + 2);
    for (double jd = jdAOS; jd <= jdLOS + 1e-12; jd += step) {
        if (cancelled()) return;
        LookAngle look;
        GeoPos    geo;
        computeMoment(propagate, obs, jd, look, geo);
        ev.samples.push_back({ jdToIntMs(jd), look, geo });
    }
}

// ========== 二分精化 AOS ==========
double OrbitCalculator::bisectAOS(PropagateFn &propagate,
                                   const Observer &obs,
                                   double jdBelow, double jdAbove) const
{
    double maskEl = obs.minElevDeg();
    // 跨窗口过境: 下界处仰角已在掩星角之上（AOS 在窗口起点之前）→ 直接取窗口起点
    if (elevationAt(propagate, obs, jdBelow) >= maskEl)
        return jdBelow;
    double lo = jdBelow, hi = jdAbove;
    for (int i = 0; i < m_cfg.bisectionIters; ++i) {
        double mid = (lo + hi) * 0.5;
        double el = elevationAt(propagate, obs, mid);
        if (el < maskEl) lo = mid;
        else             hi = mid;
    }
    return lo;
}

// ========== 二分精化 LOS ==========
double OrbitCalculator::bisectLOS(PropagateFn &propagate,
                                   const Observer &obs,
                                   double jdAbove, double jdBelow) const
{
    double maskEl = obs.minElevDeg();
    double lo = jdAbove, hi = jdBelow;
    for (int i = 0; i < m_cfg.bisectionIters; ++i) {
        double mid = (lo + hi) * 0.5;
        double el = elevationAt(propagate, obs, mid);
        if (el >= maskEl) lo = mid;
        else              hi = mid;
    }
    return lo;
}

// ========== TCA 扫描（最大仰角时刻） ==========
void OrbitCalculator::findTCA(PropagateFn &propagate,
                               const Observer &obs,
                               double aosJD, double losJD,
                               int64_t &outJdMs, double &outElDeg,
                               LookAngle &outLook, GeoPos &outGeo) const
{
    double stepDays = m_cfg.maxElevScanStepSec / 86400.0;
    double bestJD = aosJD;
    double bestEl = -1000.0;
    outJdMs = 0;   // 取消时保证不输出垃圾值

    // 粗扫描
    for (double jd = aosJD; jd <= losJD; jd += stepDays) {
        if (cancelled()) return;
        double el = elevationAt(propagate, obs, jd);
        if (el > bestEl) {
            bestEl = el;
            bestJD = jd;
        }
    }

    // 标准三分搜索精化 TCA（单峰函数保证收敛，无平台发散问题）
    const double delta = TCA_REFINE_SEC / 86400.0;  // seconds in JD
    double lo = bestJD - delta, hi = bestJD + delta;
    for (int i = 0; i < m_cfg.bisectionIters; ++i) {
        if (cancelled()) return;
        double m1 = lo + (hi - lo) / 3.0;
        double m2 = hi - (hi - lo) / 3.0;
        double el1 = elevationAt(propagate, obs, m1);
        double el2 = elevationAt(propagate, obs, m2);
        if (el1 >= el2) hi = m2;
        else            lo = m1;
    }

    bestJD = (lo + hi) * 0.5;
    outElDeg = elevationAt(propagate, obs, bestJD);
    computeMoment(propagate, obs, bestJD, outLook, outGeo);
    outJdMs = jdToIntMs(bestJD);
}

// ========== 主入口 ==========
std::vector<PassEvent> OrbitCalculator::predict(PropagateFn propagate,
                                                 const Observer &observer,
                                                 double jdStart,
                                                 double hoursAhead,
                                                 const std::atomic<bool> *cancel) const
{
    const double jdEnd    = jdStart + hoursAhead / 24.0;
    const double stepDays = m_cfg.coarseStepSec / 86400.0;
    const double maskEl   = observer.minElevDeg();
    std::vector<PassEvent> passes;

    m_cancel = cancel;
    // RAII: 任意退出路径（含取消提前返回）后清空成员指针，
    // 防调用方销毁 cancel 目标后对象被复用时读到悬垂指针
    struct CancelReset {
        const std::atomic<bool> *&ref;
        ~CancelReset() { ref = nullptr; }
    } cancelReset{m_cancel};

    // 初始状态: 若窗口起点已在掩星角之上，视为过境中（AOS 在窗口前，用窗口起点作下界）
    double el0 = elevationAt(propagate, observer, jdStart);
    bool   inPass     = (el0 >= maskEl);
    double prevEl     = el0;
    double candBelow  = jdStart;   // AOS 候选：仰角 < maskEl 的 JD
    double candAbove  = jdStart;   // AOS 候选：仰角 ≥ maskEl 的 JD

    // ---- 粗搜索 ----
    for (double jd = jdStart; jd < jdEnd; jd += stepDays) {
        if (cancelled()) return passes;   // 协作式取消

        double el = elevationAt(propagate, observer, jd);

        if (!inPass) {
            // 检测 AOS ▲
            if (prevEl < maskEl && el >= maskEl) {
                inPass = true;
                candBelow = jd - stepDays;
                candAbove = jd;
            }
        } else {
            // 检测 LOS ▼
            if (prevEl >= maskEl && el < maskEl) {
                // ---- 精化 AOS/LOS ----
                double jdAOS = bisectAOS(propagate, observer, candBelow, jd);
                double jdLOS = bisectLOS(propagate, observer, candAbove, jd);

                // 过滤短过境
                double durSec = (jdLOS - jdAOS) * 86400.0;
                if (durSec < m_cfg.minDurationSec) {
                    inPass = false;
                    prevEl = el;
                    continue;
                }

                // 组装 PassEvent
                PassEvent ev;

                // AOS 时刻详情
                computeMoment(propagate, observer, jdAOS, ev.aosLook, ev.aosSatGeo);
                ev.aosJdMs = jdToIntMs(jdAOS);

                // TCA
                findTCA(propagate, observer, jdAOS, jdLOS,
                        ev.tcaJdMs, ev.maxElevDeg, ev.tcaLook, ev.tcaSatGeo);

                // LOS 时刻详情
                computeMoment(propagate, observer, jdLOS, ev.losLook, ev.losSatGeo);
                ev.losJdMs = jdToIntMs(jdLOS);

                ev.durationSec = durSec;

                // 弧段采样（真实轨迹，供绘图/导出）
                collectSamples(propagate, observer, jdAOS, jdLOS, ev);

                // 卫星名称
                ev.satelliteName = m_satName;

                // 填充光照/折射/多普勒/类型/质量
                fillPassDetails(ev, propagate, observer, m_cfg, jdAOS,
                                intMsToJd(ev.tcaJdMs), jdLOS);

                passes.push_back(ev);
                inPass = false;
            }
        }
        prevEl = el;
    }

    // ---- 跨 24h 边界处理 ----
    if (inPass) {
        // 延长搜索最多 EXTEND_MIN 分钟
        double jdExtend = jdEnd + EXTEND_MIN / 1440.0;
        double jdLos = -1.0;
        for (double jd = jdEnd; jd < jdExtend; jd += stepDays) {
            if (cancelled()) return passes;

            double el = elevationAt(propagate, observer, jd);
            if (prevEl >= maskEl && el < maskEl) {
                jdLos = bisectLOS(propagate, observer, candAbove, jd);
                break;
            }
            prevEl = el;
        }

        // 延长窗口结束仍未见 LOS（高轨/同步卫星长过境）:
        // 以窗口终点为 LOS 近似收尾，避免过境整体丢失
        if (jdLos < 0.0)
            jdLos = jdExtend;

        double jdAOS = bisectAOS(propagate, observer, candBelow, jdLos);
        double durSec = (jdLos - jdAOS) * 86400.0;
        if (durSec >= m_cfg.minDurationSec) {
            PassEvent ev;
            computeMoment(propagate, observer, jdAOS, ev.aosLook, ev.aosSatGeo);
            ev.aosJdMs = jdToIntMs(jdAOS);
            findTCA(propagate, observer, jdAOS, jdLos,
                    ev.tcaJdMs, ev.maxElevDeg, ev.tcaLook, ev.tcaSatGeo);
            computeMoment(propagate, observer, jdLos, ev.losLook, ev.losSatGeo);
            ev.losJdMs = jdToIntMs(jdLos);
            ev.durationSec = durSec;
            collectSamples(propagate, observer, jdAOS, jdLos, ev);
            ev.satelliteName = m_satName;
            fillPassDetails(ev, propagate, observer, m_cfg, jdAOS,
                            intMsToJd(ev.tcaJdMs), jdLos);
            passes.push_back(ev);
        }
    }

    // ---- 合并相邻过境 ----
    if (!cancelled() && passes.size() > 1 && m_cfg.mergeGapSec > 0.0) {
        const double gapDays = m_cfg.mergeGapSec / 86400.0;
        std::vector<PassEvent> merged;
        merged.push_back(std::move(passes[0]));
        for (size_t i = 1; i < passes.size(); ++i) {
            double prevLos = intMsToJd(merged.back().losJdMs);
            double curAos  = intMsToJd(passes[i].aosJdMs);
            if (curAos - prevLos < gapDays) {
                // 合并：用较晚的 LOS 替换
                if (passes[i].losJdMs > merged.back().losJdMs) {
                    merged.back().losJdMs = passes[i].losJdMs;
                    merged.back().losLook = passes[i].losLook;
                    merged.back().losSatGeo = passes[i].losSatGeo;
                    merged.back().losSunAlt = passes[i].losSunAlt;
                    merged.back().losSatShadow = passes[i].losSatShadow;
                    merged.back().losElRefracted = passes[i].losElRefracted;
                    merged.back().losIllum = passes[i].losIllum;
                    merged.back().durationSec = (intMsToJd(merged.back().losJdMs) - intMsToJd(merged.back().aosJdMs)) * 86400.0;
                    // 弧段采样: 追加后半段的点（保持时间单调，跳过重叠区）
                    auto &dst = merged.back().samples;
                    const int64_t lastT = dst.empty() ? std::numeric_limits<int64_t>::min()
                                                      : dst.back().jdMs;
                    for (const auto &s : passes[i].samples)
                        if (s.jdMs > lastT) dst.push_back(s);
                }
                // 取较高的最大仰角
                if (passes[i].maxElevDeg > merged.back().maxElevDeg) {
                    merged.back().maxElevDeg = passes[i].maxElevDeg;
                    merged.back().tcaJdMs = passes[i].tcaJdMs;
                    merged.back().tcaLook = passes[i].tcaLook;
                    merged.back().tcaSatGeo = passes[i].tcaSatGeo;
                    merged.back().tcaSunAlt = passes[i].tcaSunAlt;
                    merged.back().tcaSatShadow = passes[i].tcaSatShadow;
                    merged.back().tcaIllum = passes[i].tcaIllum;
                    merged.back().tcaElRefracted = passes[i].tcaElRefracted;
                    merged.back().dopplerAtTcaKHz = passes[i].dopplerAtTcaKHz;
                }
            } else {
                merged.push_back(std::move(passes[i]));
            }
        }
        passes = std::move(merged);
    }

    // ---- 排序: 按 AOS 升序 ----
    std::sort(passes.begin(), passes.end(),
              [](const PassEvent &a, const PassEvent &b) {
                  return a.aosJdMs < b.aosJdMs;
              });

    return passes;
}
