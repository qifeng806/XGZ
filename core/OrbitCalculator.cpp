// =============================================================================
// OrbitCalculator.cpp — 过境预报引擎完整实现
// =============================================================================
#include "OrbitCalculator.h"
#include "CoordTransform.h"
#include "SunPosition.h"
#include "EarthShadow.h"
#include "DopplerCalc.h"
#include "Magnitude.h"
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
    // 注: 地固系旋转（GMST）用 UT1 ≈ UTC（≤ 0.9s）的儒略日即可；
    // ΔT（TT−UT1）只用于太阳历表（SunPosition 内部自行转为 TT），切不可加到这里
}

// ========== 辅助：填充一次过境的完整详情 ==========
static void fillPassDetails(PassEvent &ev,
    OrbitCalculator::PropagateFn &propagate, const Observer &observer,
    const OrbitCalculator::Config &cfg,
    double jdAOS, double jdTCA, double jdLOS)
{
    // TCA 时刻轨道状态: 地影/星等/多普勒共用一次传播，避免重复调用 SGP4
    std::optional<EciState> eciT;
    if (cfg.enableShadowCheck || cfg.enableMagnitude || cfg.enableDoppler)
        eciT = propagate(jdTCA);

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
            auto eciL = propagate(jdLOS);
            if (eciA) {
                auto eA = CoordTransform::eciToEcef(eciA->posKm, jdAOS);
                // 太阳方向必须旋转到 ECEF 与卫星位置同系
                auto sunDirA = CoordTransform::eciDirToEcef(sunA.dirECI, jdAOS);
                ev.aosSatShadow = EarthShadow::checkShadow(eA, sunDirA, sunA.distanceAU);
                ev.aosIllum = EarthShadow::umbraFactor(eA, sunDirA, sunA.distanceAU);
            }
            if (eciT) {
                auto eT = CoordTransform::eciToEcef(eciT->posKm, jdTCA);
                auto sunDirT = CoordTransform::eciDirToEcef(sunT.dirECI, jdTCA);
                ev.tcaSatShadow = EarthShadow::checkShadow(eT, sunDirT, sunT.distanceAU);
                ev.tcaIllum = EarthShadow::umbraFactor(eT, sunDirT, sunT.distanceAU);
            }
            if (eciL) {
                auto eL = CoordTransform::eciToEcef(eciL->posKm, jdLOS);
                auto sunDirL = CoordTransform::eciDirToEcef(sunL.dirECI, jdLOS);
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

        // 星等估算: Vallerie 1963 相位模型，取 TCA 时刻（与可见性分类同一观测时刻）
        // m = m₀ − 15 + 5·log10(R) − 2.5·log10(f(B))，B = 卫星处太阳-观测者张角
        if (cfg.enableMagnitude) {
            if (eciT) {
                auto satE = CoordTransform::eciToEcef(eciT->posKm, jdTCA);
                // 太阳 ECEF 位置（km）: 单位方向 × 日地距离
                auto sunDirE = CoordTransform::eciDirToEcef(sunT.dirECI, jdTCA);
                std::array<double, 3> sunPosE;
                for (int k = 0; k < 3; ++k)
                    sunPosE[k] = sunDirE[k] * (sunT.distanceAU * EarthShadow::AU_KM);
                const double phase = MagnitudeCalc::phaseAngleRad(satE, sunPosE,
                                                                  observer.ecef());
                ev.magAtTca = MagnitudeCalc::visualMagnitude(
                    MagnitudeCalc::standardMagnitude(ev.satelliteName, cfg.stdMagFallback),
                    ev.tcaLook.rangeKm, phase);
                // 本影中卫星不可见，公式前提（太阳无遮挡）不成立 → 标为无效
                ev.magValid = (ev.tcaSatShadow != ShadowStatus::InUmbra)
                              && std::isfinite(ev.magAtTca);
            }
        }
    }
    if (cfg.enableRefraction) {
        ev.aosElRefracted = CoordTransform::applyRefraction(ev.aosLook.elevDeg);
        ev.tcaElRefracted = CoordTransform::applyRefraction(ev.tcaLook.elevDeg);
        ev.losElRefracted = CoordTransform::applyRefraction(ev.losLook.elevDeg);
    }
    if (cfg.enableDoppler) {
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

    auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
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
    auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
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
    // 传播一次完成全部字段（含多普勒），避免对同一时刻二次传播
    for (double jd = jdAOS; jd <= jdLOS + 1e-12; jd += step) {
        if (cancelled()) return;
        auto eciOpt = propagate(jd);
        LookAngle look;
        GeoPos    geo;
        double    dopplerKHz = 0.0;
        if (eciOpt) {
            auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
            look = obs.lookAngle(ecef);
            geo  = obs.subSatPoint(ecef);
            if (m_cfg.enableDoppler) {
                dopplerKHz = DopplerCalc::compute(eciOpt->posKm, eciOpt->velKms, jd,
                                                  obs.ecef(), m_cfg.carrierFreqMHz);
            }
        } else {
            look = LookAngle(-1000.0, 0.0, 0.0);   // 传播失败占位（与 computeMoment 一致）
            geo  = GeoPos(0.0, 0.0, 0.0);
        }
        ev.samples.push_back({ jdToIntMs(jd), look, geo, dopplerKHz });
    }
}

// ========== 可见弧段子区间 ==========
// Heavens-Above 语义: 几何过境内"地面暗天（太阳高度 < 阈值）+ 卫星被照亮
// （太阳圆面可见比例 > 50%）"的最大连续子区间，即观测者实际能看到的时段。
// 粗扫描定位状态翻转区间 → 二分精化边界；TCA 作为采样点强制纳入网格，
// 保证与 TCA 时刻可见性分类（fillPassDetails）严格一致。
void OrbitCalculator::collectVisibleArc(PropagateFn &propagate,
                                        const Observer &obs,
                                        double jdAOS, double jdLOS,
                                        PassEvent &ev) const
{
    if (!m_cfg.enableVisArc || !m_cfg.enableSunAnalysis
        || !m_cfg.enableShadowCheck || m_cfg.sampleStepSec <= 0.0) return;
    if (jdLOS <= jdAOS) return;

    const double step  = m_cfg.sampleStepSec / 86400.0;
    const double dark  = m_cfg.darkSkyThresholdDeg;

    // t 时刻是否"可观测": 地面暗天 且 卫星被照亮（光照圆面 > 50%）
    auto visibleAt = [&](double jd) -> bool {
        if (cancelled()) return false;
        auto sun = SunPositionCalc::compute(jd, obs.latDeg(), obs.lonDeg());
        if (sun.altDeg >= dark) return false;
        auto eciOpt = propagate(jd);
        if (!eciOpt) return false;
        auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
        auto sunDirE = CoordTransform::eciDirToEcef(sun.dirECI, jd);
        return EarthShadow::umbraFactor(ecef, sunDirE, sun.distanceAU) > 0.5;
    };

    // 二分精化状态翻转边界: [lo, hi] 端点可见性已知不同，收敛到区间内的翻转点。
    // 不变式: visibleAt(lo) == A, visibleAt(hi) == B（A ≠ B），收敛到翻转位置
    auto refineBoundary = [&](double jdLo, double jdHi) -> double {
        for (int i = 0; i < m_cfg.bisectionIters; ++i) {
            double mid = (jdLo + jdHi) * 0.5;
            if (visibleAt(mid)) jdHi = mid;
            else                jdLo = mid;
        }
        return (jdLo + jdHi) * 0.5;
    };

    // 采样网格: 等步长 + 强制纳入 TCA（保证相邻采样状态不同处边界精化正确，
    // 且 TCA 时刻可见性分类与弧段包含关系严格一致）
    std::vector<double> jds;
    jds.push_back(jdAOS);
    for (double jd = jdAOS + step; jd < jdLOS; jd += step) jds.push_back(jd);
    jds.push_back(jdLOS);
    {
        double tca = intMsToJd(ev.tcaJdMs);
        if (tca > jdAOS && tca < jdLOS) {
            // 插入 TCA（若已与某采样点重合则跳过）
            bool dup = false;
            for (double jd : jds) {
                if (std::abs(jd - tca) * 86400.0 < 0.5) { dup = true; break; }
            }
            if (!dup) {
                jds.push_back(tca);
                std::sort(jds.begin(), jds.end());
            }
        }
    }

    // 逐采样点求可见性，收集连续 true 段
    std::vector<bool> vis(jds.size());
    for (size_t i = 0; i < jds.size(); ++i) vis[i] = visibleAt(jds[i]);

    struct Run { int s, e; };   // 闭区间 [s, e]
    std::vector<Run> runs;
    size_t i = 0;
    while (i < jds.size()) {
        if (!vis[i]) { ++i; continue; }
        size_t s = i;
        while (i < jds.size() && vis[i]) ++i;
        runs.push_back({ static_cast<int>(s), static_cast<int>(i - 1) });
    }
    if (runs.empty()) return;

    // 多段时取时间跨度最长的连续段（语义: 最大连续可见子区间；
    // 若取首段起点+末段终点，中间不可见间隙会被错误包含）
    size_t best = 0;
    for (size_t k = 1; k < runs.size(); ++k) {
        if (jds[runs[k].e] - jds[runs[k].s]
            > jds[runs[best].e] - jds[runs[best].s])
            best = k;
    }
    // 该段前后边界: 内部采样直接取端点，否则二分精化翻转区间
    double b0 = (runs[best].s > 0)
        ? refineBoundary(jds[runs[best].s - 1], jds[runs[best].s])
        : jdAOS;
    double b1 = (runs[best].e < static_cast<int>(jds.size()) - 1)
        ? refineBoundary(jds[runs[best].e], jds[runs[best].e + 1])
        : jdLOS;

    ev.hasVisibleArc = true;
    ev.visAosJdMs = jdToIntMs(b0);
    ev.visLosJdMs = jdToIntMs(b1);
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
    outJdMs  = 0;       // 取消时保证不输出垃圾值
    outElDeg = -1000.0; // 取消时保证不输出垃圾值

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
        std::atomic<const std::atomic<bool> *> &ref;
        ~CancelReset() { ref = nullptr; }
    } cancelReset{m_cancel};

    // 初始状态: 若窗口起点已在掩星角之上，视为过境中（AOS 在窗口前，用窗口起点作下界）
    double el0 = elevationAt(propagate, observer, jdStart);
    bool   inPass     = (el0 >= maskEl);
    double prevEl     = el0;
    double candBelow  = jdStart;   // AOS 候选：仰角 < maskEl 的 JD
    double candAbove  = jdStart;   // AOS 候选：仰角 ≥ maskEl 的 JD

    // ---- 粗搜索 ----
    // 末次采样推进到窗口终点（jdEnd），防窗口末尾一个粗搜步长内的 AOS 漏检;
    // 对完全落在相邻两采样点之间的短过境做中点探测
    auto assemblePass = [&](double jdAOS, double jdLOS) -> bool {
        double durSec = (jdLOS - jdAOS) * 86400.0;
        if (durSec < m_cfg.minDurationSec) return false;
        PassEvent ev;
        computeMoment(propagate, observer, jdAOS, ev.aosLook, ev.aosSatGeo);
        ev.aosJdMs = jdToIntMs(jdAOS);
        // TCA（findTCA 内部取消时输出已知哨兵值，这里再检查一次取消）
        findTCA(propagate, observer, jdAOS, jdLOS,
                ev.tcaJdMs, ev.maxElevDeg, ev.tcaLook, ev.tcaSatGeo);
        if (cancelled()) return false;
        computeMoment(propagate, observer, jdLOS, ev.losLook, ev.losSatGeo);
        ev.losJdMs = jdToIntMs(jdLOS);
        ev.durationSec = durSec;
        collectSamples(propagate, observer, jdAOS, jdLOS, ev);
        ev.satelliteName = m_satName;
        fillPassDetails(ev, propagate, observer, m_cfg, jdAOS,
                        intMsToJd(ev.tcaJdMs), jdLOS);
        collectVisibleArc(propagate, observer, jdAOS,
                          intMsToJd(ev.losJdMs), ev);
        passes.push_back(ev);
        return true;
    };

    for (double jd = jdStart;; jd += stepDays) {
        if (cancelled()) return passes;   // 协作式取消

        double el = elevationAt(propagate, observer, jd);

        if (!inPass) {
            // 检测 AOS ▲
            if (prevEl < maskEl && el >= maskEl) {
                inPass = true;
                candBelow = jd - stepDays;
                candAbove = jd;
            } else if (prevEl < maskEl && jd - stepDays >= jdStart) {
                // 步内短过境探测: 相邻两次采样之间 AOS/LOS 均出现（中点高于掩星角）
                double jdMid = jd - 0.5 * stepDays;
                if (elevationAt(propagate, observer, jdMid) >= maskEl) {
                    assemblePass(
                        bisectAOS(propagate, observer, jd - stepDays, jdMid),
                        bisectLOS(propagate, observer, jdMid, jd));
                }
            }
        } else {
            // 检测 LOS ▼
            if (prevEl >= maskEl && el < maskEl) {
                double jdAOS = bisectAOS(propagate, observer, candBelow, jd);
                double jdLOS = bisectLOS(propagate, observer, candAbove, jd);
                assemblePass(jdAOS, jdLOS);
                inPass = false;
            }
        }
        prevEl = el;
        if (jd >= jdEnd) break;
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
        assemblePass(jdAOS, jdLos);
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
                    // 可见弧段: 合并后取两段并集区间
                    if (passes[i].hasVisibleArc && merged.back().hasVisibleArc) {
                        merged.back().visAosJdMs = std::min(merged.back().visAosJdMs,
                                                            passes[i].visAosJdMs);
                        merged.back().visLosJdMs = std::max(merged.back().visLosJdMs,
                                                            passes[i].visLosJdMs);
                    } else if (passes[i].hasVisibleArc) {
                        merged.back().hasVisibleArc = true;
                        merged.back().visAosJdMs = passes[i].visAosJdMs;
                        merged.back().visLosJdMs = passes[i].visLosJdMs;
                    }
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
                    merged.back().magAtTca = passes[i].magAtTca;
                    merged.back().magValid = passes[i].magValid;
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
