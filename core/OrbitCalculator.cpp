// =============================================================================
// OrbitCalculator.cpp — 过境预报引擎完整实现
// =============================================================================
#include "OrbitCalculator.h"
#include "CoordTransform.h"
#include "SunPosition.h"
#include "EarthShadow.h"
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
    constexpr double TCA_REFINE_SEC = 3.0;     // TCA 三分搜索初始半宽（秒）
    constexpr double SEC_PER_DAY  = 86400.0;
    // 传播失败哨兵: 真实仰角物理下限为 -90°, 该值不可能由观测产生。
    // 调用方必须用它区分"传播失败"与"真实低于掩星角"——把失败点当真实
    // LOS/AOS 会把过境错误分裂或截断（瞬态失败时仰角其实仍在掩星角之上）
    constexpr double ELEV_FAILURE_SENTINEL = -1000.0;
    // 弧段采样点 / 可见性网格的**统一点数上限**: 逐点含 propagate(+太阳位置+
    // 地影) 计算, 无界耗时会把 worker 挂死。此前的两个 100000 各自为政且
    // 语义不同（采样点 vs 可见性检查点）, 超限时直接静默放弃全部数据——
    // 现统一为本常量, 超限方按"放大步长"降级覆盖整段, 不再丢数据
    constexpr int kMaxArcSamples = 100000;
    // 观测站海拔的地平线倾角（度）: dip = acos(r/(r+h)) ≈ 1.0152·sqrt(h_km)。
    // 高海拔站"视地平线"下倾，暗天判定的太阳高度阈值须按此值平移——
    // 否则海拔 1km 山地站在太阳尚高于其视地平时就被误判为暗天
    constexpr double DIP_COEF_DEG_PER_SQRT_KM = 1.0152;
    inline double horizonDipDeg(double altKm)
    {
        return DIP_COEF_DEG_PER_SQRT_KM * std::sqrt(std::max(0.0, altKm));
    }
    // 注: 地固系旋转（GMST）用 UT1 ≈ UTC（≤ 0.9s）的儒略日即可；
    // ΔT（TT−UT1）只用于太阳历表（SunPosition 内部自行转为 TT），切不可加到这里

    // ---- 采样点数换算（含端点 / 不含端点）----
    // 直接把"窗口 ÷ 步长"取整会踩到 jdLOS 自身的**表示舍入**：
    // JD≈2.46e6 时 ulp≈4.7e-10 天（≈40 µs），整除窗口可能被算成 59.999997 步，
    // floor 之后丢掉端点那一个采样点（表现为弧段采样/可见弧段网格少一点）。
    // 这里把 4×ulp 折算成"步"再加进去：既保留"含端点"的语义，
    // 又不会凭空多采（该补偿量远小于 1 步，除极小步长外不会引入额外采样）。
    inline double ulpDaysAt(double jd)
    {
        return 4.0 * std::numeric_limits<double>::epsilon() * std::fabs(jd);
    }
    inline long long stepCountInclusive(double jdA, double jdB, double stepDays)
    {
        const double eps = ulpDaysAt(std::max(std::fabs(jdA), std::fabs(jdB))) / stepDays;
        return static_cast<long long>(std::floor((jdB - jdA) / stepDays + eps + 1e-12));
    }
    inline long long stepCountExclusive(double jdA, double jdB, double stepDays)
    {
        const double eps = ulpDaysAt(std::max(std::fabs(jdA), std::fabs(jdB))) / stepDays;
        return static_cast<long long>(std::floor((jdB - jdA) / stepDays - eps + 1e-12));
    }
}

// ========== 辅助：填充一次过境的完整详情 ==========
static void fillPassDetails(PassEvent &ev,
    OrbitCalculator::PropagateFn &propagate, const Observer &observer,
    const OrbitCalculator::Config &cfg,
    double jdAOS, double jdTCA, double jdLOS)
{
    try {
        // 优化：批量获取所有需要的轨道状态，避免重复调用 propagate
        std::optional<EciState> eciA, eciT, eciL;
        std::optional<SunPosition> sunT;   // TCA 时刻太阳位置（光照分析用）
        
        // 仅在需要时才进行轨道传播
        if (cfg.enableSunAnalysis && cfg.enableShadowCheck) {
            // 需要所有三个时刻的数据
            eciA = propagate(jdAOS);
            eciT = propagate(jdTCA);
            eciL = propagate(jdLOS);
        } else if (cfg.enableShadowCheck) {
            // 只需要 TCA 时刻的数据
            eciT = propagate(jdTCA);
        }
        
        // 光照分析
        if (cfg.enableSunAnalysis) {
            const auto sunA = SunPositionCalc::compute(jdAOS, observer.latDeg(), observer.lonDeg());
            sunT = SunPositionCalc::compute(jdTCA, observer.latDeg(), observer.lonDeg());
            const auto sunL = SunPositionCalc::compute(jdLOS, observer.latDeg(), observer.lonDeg());
            
            // 太阳位置计算失败（输入非法）: 与下方"传播失败"同样处理——
            // 由外层 try/catch 落为 Daylight/默认值, 绝不用伪造的 0 值参与判定
            if (!sunA || !sunT || !sunL) {
                throw std::runtime_error("太阳位置计算失败");
            }
            
            ev.aosSunAlt = sunA->altDeg;
            ev.tcaSunAlt = sunT->altDeg;
            ev.losSunAlt = sunL->altDeg;

            if (cfg.enableShadowCheck) {
                if (eciA) {
                    auto eA = CoordTransform::eciToEcef(eciA->posKm, jdAOS);
                    // 太阳方向必须旋转到 ECEF 与卫星位置同系
                    auto sunDirA = CoordTransform::eciDirToEcef(sunA->dirECI, jdAOS);
                    ev.aosSatShadow = EarthShadow::checkShadow(eA, sunDirA, sunA->distanceAU);
                    ev.aosIllum = EarthShadow::umbraFactor(eA, sunDirA, sunA->distanceAU);
                } else {
                    // 传播失败: 不得让 PassEvent 保持结构体默认值 FullSunlight/1.0
                    // —— 那会把失败时刻"默认为照亮", 暗天过境被误判为可见
                    ev.aosSatShadow = ShadowStatus::InUmbra;
                    ev.aosIllum = 0.0;
                }
                if (eciT) {
                    auto eT = CoordTransform::eciToEcef(eciT->posKm, jdTCA);
                    auto sunDirT = CoordTransform::eciDirToEcef(sunT->dirECI, jdTCA);
                    ev.tcaSatShadow = EarthShadow::checkShadow(eT, sunDirT, sunT->distanceAU);
                    ev.tcaIllum = EarthShadow::umbraFactor(eT, sunDirT, sunT->distanceAU);
                } else {
                    ev.tcaSatShadow = ShadowStatus::InUmbra;
                    ev.tcaIllum = 0.0;
                }
                if (eciL) {
                    auto eL = CoordTransform::eciToEcef(eciL->posKm, jdLOS);
                    auto sunDirL = CoordTransform::eciDirToEcef(sunL->dirECI, jdLOS);
                    ev.losSatShadow = EarthShadow::checkShadow(eL, sunDirL, sunL->distanceAU);
                    ev.losIllum = EarthShadow::umbraFactor(eL, sunDirL, sunL->distanceAU);
                } else {
                    ev.losSatShadow = ShadowStatus::InUmbra;
                    ev.losIllum = 0.0;
                }
            }

            // 光照判定以 TCA（用户实际观测时刻）为准 —— 与 Heavens-Above/Orbis
            // 的「峰值点燃亮」语义一致：仅 AOS/LOS 照亮、TCA 却在地影内的过境
            // 不标为可见（否则用户按 TCA 时刻观测看不到卫星）
            // 注: 使用 dip 修正后的阈值，与 visibleArc 保持一致
            bool satLit = (ev.tcaSatShadow != ShadowStatus::InUmbra) && ev.tcaIllum > 0.5;
            const double darkThreshold = cfg.darkSkyThresholdDeg - horizonDipDeg(observer.altKm());
            if (ev.tcaSunAlt >= darkThreshold)
                ev.visibility = PassVisibility::Daylight;
            else if (!satLit)
                ev.visibility = PassVisibility::RadioOnly;
            else
                ev.visibility = PassVisibility::Visible;
        }

        // 质量等级
        if (ev.maxElevDeg >= 50.0)      ev.quality = PassQuality::Excellent;
        else if (ev.maxElevDeg >= 20.0) ev.quality = PassQuality::Good;
        else                            ev.quality = PassQuality::Poor;
    } catch (const std::exception &e) {
        // 异常情况下设置默认值，避免数据损坏
        ev.visibility = PassVisibility::Daylight;
        ev.quality = PassQuality::Poor;
        
        #ifdef QT_DEBUG
        qDebug("fillPassDetails 异常 (sat=%s): %s", ev.satelliteName.c_str(), e.what());
        #endif
    }
}

int64_t OrbitCalculator::jdToIntMs(double jd) {
    // 委托给 DataTypes.h 的全局 jdToMs 消除重复实现
    return jdToMs(jd);
}
double OrbitCalculator::intMsToJd(int64_t ms) {
    // 委托给 DataTypes.h 的全局 msToJd 消除重复实现
    return msToJd(ms);
}

// ============================================================================
// propagate / search / bisect / collectVisibleArc / collectSamples
// ============================================================================

// ========== 单点仰角计算 ==========
double OrbitCalculator::elevationAt(PropagateFn &propagate,
                                     const Observer &obs, double jd) const
{
    try {
        // 检查输入参数有效性
        if (!std::isfinite(jd)) {
            return ELEV_FAILURE_SENTINEL;
        }
        
        auto eciOpt = propagate(jd);
        if (!eciOpt) return ELEV_FAILURE_SENTINEL;  // 传播失败 → 调用方按哨兵跳过, 不得视作真实 LOS

        auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
        auto look = obs.lookAngle(ecef);
        
        // 检查仰角数据有效性
        if (!std::isfinite(look.elevDeg)) {
            return ELEV_FAILURE_SENTINEL;
        }
        
        return look.elevDeg;
    } catch (const std::exception &e) {
        // 异常情况下返回失败哨兵值
        return ELEV_FAILURE_SENTINEL;
    }
}

// ========== 计算某一时刻的完整观测 + 星下点 ==========
void OrbitCalculator::computeMoment(PropagateFn &propagate,
                                     const Observer &obs, double jd,
                                     LookAngle &look, GeoPos &geo) const
{
    try {
        // 检查输入参数有效性
        if (!std::isfinite(jd)) {
            look = LookAngle(ELEV_FAILURE_SENTINEL, 0.0, 0.0);
            geo  = GeoPos(0.0, 0.0, 0.0);
            return;
        }
        
        auto eciOpt = propagate(jd);
        if (!eciOpt) {
            look = LookAngle(ELEV_FAILURE_SENTINEL, 0.0, 0.0);
            geo  = GeoPos(0.0, 0.0, 0.0);
            return;
        }
        
        auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
        look = obs.lookAngle(ecef);
        geo  = obs.subSatPoint(ecef);
        
        // 检查结果有效性
        if (!std::isfinite(look.elevDeg) || !std::isfinite(look.azDeg) || !std::isfinite(look.rangeKm) ||
            !std::isfinite(geo.latDeg) || !std::isfinite(geo.lonDeg) || !std::isfinite(geo.altKm)) {
            look = LookAngle(ELEV_FAILURE_SENTINEL, 0.0, 0.0);
            geo  = GeoPos(0.0, 0.0, 0.0);
        }
    } catch (const std::exception &e) {
        // 异常情况下设置默认值
        look = LookAngle(ELEV_FAILURE_SENTINEL, 0.0, 0.0);
        geo  = GeoPos(0.0, 0.0, 0.0);
    }
}

// ========== 过境弧段采样 ==========
void OrbitCalculator::collectSamples(PropagateFn &propagate,
                                       const Observer &obs,
                                       double jdAOS, double jdLOS,
                                       PassEvent &ev,
                                       const AtomicCancelFlagPtr cancel) const
{
    try {
        if (!m_cfg.enableSamples || m_cfg.sampleStepSec <= 0.0) return;
        
        // 检查时间范围有效性
        if (!std::isfinite(jdAOS) || !std::isfinite(jdLOS) || jdAOS >= jdLOS) {
            return;
        }
        
        // 弧段跨距（防御: 调用链保证 jdLOS > jdAOS, 但数值抖动/上游配置错误
        // 时负跨距会在 size_t 转换后变成天文数字触发 bad_alloc）
        const double span = jdLOS - jdAOS;
        ev.samples.clear();
        // 步长降级: 超过上限时放大步长以覆盖整段, 而非旧实现的"静默放弃全部
        // 采样"（用户配置 1 s 步长 + 48 h 窗口会得到空曲线/空 KML, 却无任何提示）
        double step = m_cfg.sampleStepSec / 86400.0;
        if (span / step > static_cast<double>(kMaxArcSamples)) {
            step = span / static_cast<double>(kMaxArcSamples);
            qWarning("collectSamples: 采样点数超过 %d 上限, 步长由 %.3f 秒降级为 %.3f 秒",
                     kMaxArcSamples, m_cfg.sampleStepSec, step * 86400.0);
        }
        const double n = std::max(0.0, span / step);
        // 预分配：基础点数 + 余量
        size_t estimatedSamples = static_cast<size_t>(n) + 2;
        estimatedSamples += estimatedSamples / 4;  // +25% 防重分配
        ev.samples.reserve(estimatedSamples);
        
        // 传播一次完成全部字段，避免对同一时刻二次传播
        // 采样时刻用"起点 + 整数索引 × 步长"：逐次 jd += step 会累积浮点误差
        // （JD≈2.46e6 时 ulp≈41 µs/步，24h/15s 窗口累计可达 ~5 ms ≈ 40 m 位置偏差），
        // 与 STK 等外部工具做逐点比对时该偏差不可忽略；点数含端点
        const long long nSamples = stepCountInclusive(jdAOS, jdLOS, step);
        for (long long i = 0; i <= nSamples; ++i) {
            const double jd = jdAOS + static_cast<double>(i) * step;
            if (isCancelled(cancel)) return;
            
            try {
                auto eciOpt = propagate(jd);
                LookAngle look;
                GeoPos geo;
                
                if (eciOpt) {
                    auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
                    look = obs.lookAngle(ecef);
                    geo = obs.subSatPoint(ecef);
                } else {
                    look = LookAngle(ELEV_FAILURE_SENTINEL, 0.0, 0.0);   // 传播失败占位（与 computeMoment 一致）
                    geo = GeoPos(0.0, 0.0, 0.0);
                }
                
                // 检查采样数据有效性（仰角必须在物理范围内，传播失败占位 -1000° 需排除）
                if (std::isfinite(look.elevDeg) && std::isfinite(look.azDeg) && 
                    std::isfinite(look.rangeKm) && std::isfinite(geo.latDeg) && 
                    std::isfinite(geo.lonDeg) && std::isfinite(geo.altKm)
                    && look.elevDeg > -90.0 && look.elevDeg <= 90.0) {
                    PassSample s;
                    s.jdMs = jdToIntMs(jd);
                    s.look = look;
                    s.geo = geo;
                    ev.samples.push_back(s);
                }
            } catch (const std::exception &e) {
                // 单个采样点失败时跳过该点，继续处理其他点
                continue;
            }
        }
    } catch (const std::bad_alloc &e) {
        // 内存分配失败时清空采样数据
        ev.samples.clear();
    } catch (const std::exception &e) {
        // 其他异常时清空采样数据
        ev.samples.clear();
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
                                         PassEvent &ev,
                                         const AtomicCancelFlagPtr cancel) const
{
    if (!m_cfg.enableVisArc || !m_cfg.enableSunAnalysis
        || !m_cfg.enableShadowCheck || m_cfg.sampleStepSec <= 0.0) return;
    if (jdLOS <= jdAOS) return;

    const double dark  = m_cfg.darkSkyThresholdDeg - horizonDipDeg(obs.altKm());
    // 网格步长: 与 collectSamples 共用同一上限常量。超限时放大步长降级
    //（可见弧段分辨率随之变粗, 但不丢结果）——旧实现直接 return, 使
    // "可见弧段"整列变空且无任何提示
    double step = m_cfg.sampleStepSec / 86400.0;
    if ((jdLOS - jdAOS) / step > static_cast<double>(kMaxArcSamples)) {
        step = (jdLOS - jdAOS) / static_cast<double>(kMaxArcSamples);
        qWarning("collectVisibleArc: 网格点数超过 %d 上限, 步长由 %.3f 秒降级为 %.3f 秒",
                 kMaxArcSamples, m_cfg.sampleStepSec, step * 86400.0);
    }

    // 太阳位置: 直接计算。等步长网格 + 二分精化下每次查询的 JD 均不同,
    // 曾用「容差 1e-8 天 + 线性扫描 100 项」的缓存从不命中, 纯开销——删除
    auto getSunPosition = [&](double jd) -> std::optional<SunPosition> {
        return SunPositionCalc::compute(jd, obs.latDeg(), obs.lonDeg());
    };
    
    // t 时刻是否"可观测": 地面暗天 且 卫星被照亮（光照圆面 > 50%）
    auto visibleAt = [&](double jd) -> bool {
        if (isCancelled(cancel)) return false;
        
        auto sunOpt = getSunPosition(jd);
        if (!sunOpt) return false;
        const auto& sun = *sunOpt;
        
        if (sun.altDeg >= dark) return false;
        
        auto eciOpt = propagate(jd);
        if (!eciOpt) return false;
        
        auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
        auto sunDirE = CoordTransform::eciDirToEcef(sun.dirECI, jd);
        return EarthShadow::umbraFactor(ecef, sunDirE, sun.distanceAU) > 0.5;
    };

    // 二分精化状态翻转边界: 调用方保证（由 vis[] 数组验证）区间内
    // 恰有一个端点可见、另一个不可见, 收敛到两者之间的翻转点。
    // jdVisibleHint = 调用方**已确认可见**的那一端。
    // 防御: 若区间内存在多次翻转（半影边缘抖动/阈值边界叠加的罕见情况）,
    // 二分会收敛到任意一个翻转点而非首/末边界——此时退化为返回
    // jdVisibleHint（真实可见段的成员）, 绝不把"不可见时刻"当作
    // 可见段端点返回（旧实现固定返回 jdLo: 对入境边界 jdLo 恰是不可见端,
    // 会产出"起点不可见"的可见弧段, 用户按它去观测什么也看不到）;
    // 取消/传播失败时同样返回 jdVisibleHint
    auto refineBoundary = [&](double jdLo, double jdHi, double jdVisibleHint) -> double {
        // 端点可见性只在被更新时重算（每次 visibleAt 含 SunPosition +
        // propagate + 地影, 是最贵的调用）——旧实现每轮重算两端, 2×迭代次数
        bool vLo = visibleAt(jdLo);
        bool vHi = visibleAt(jdHi);
        for (int i = 0; i < m_cfg.bisectionIters; ++i) {
            if (isCancelled(cancel)) return jdVisibleHint;
            if (vLo == vHi) return jdVisibleHint;   // 多翻转/端点同态: 不精化
            double mid = (jdLo + jdHi) * 0.5;
            if (visibleAt(mid)) { jdHi = mid; vHi = true; }
            else                { jdLo = mid; vLo = false; }
        }
        return (jdLo + jdHi) * 0.5;
    };

    // 采样网格: 等步长 + 强制纳入 TCA（保证相邻采样状态不同处边界精化正确，
    // 且 TCA 时刻可见性分类与弧段包含关系严格一致）
    // 点数上限已在上方按 kMaxArcSamples 通过放大步长消化, 此处不再早退
    std::vector<double> jds;
    jds.push_back(jdAOS);
    // 网格用整数索引步进（避免 jd += step 的累积漂移，见 collectSamples 说明）；
    // 内点严格早于 jdLOS，端点由下方显式压入（点数换算见 stepCountExclusive）
    {
        const long long nInner = stepCountExclusive(jdAOS, jdLOS, step);
        for (long long i = 1; i <= nInner; ++i) {
            const double jd = jdAOS + static_cast<double>(i) * step;
            if (jd >= jdLOS) break;
            jds.push_back(jd);
        }
    }
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

    // 多段时优先取包含 TCA 的连续段（可见弧段与可见性分类同以 TCA 时刻为准,
    // 避免分类为 Visible 时"弧段不含 TCA"的自相矛盾）；
    // TCA 不在任何可见段内时取最长段——这不是理论矛盾而是**常见情形**:
    // TCA 时刻卫星已入本影(visibility=RadioOnly)或天还亮着(Daylight)时,
    // 可见窗口只出现在过境两端, 弧段与 TCA 无关
    size_t best = runs.size();
    const double tcaJd = intMsToJd(ev.tcaJdMs);
    for (size_t k = 0; k < runs.size(); ++k) {
        if (jds[runs[k].s] <= tcaJd && tcaJd <= jds[runs[k].e]) {
            best = k;
            break;
        }
    }
    if (best == runs.size()) {
        best = 0;
        for (size_t k = 1; k < runs.size(); ++k) {
            if (jds[runs[k].e] - jds[runs[k].s]
                > jds[runs[best].e] - jds[runs[best].s])
                best = k;
        }
    }
    // 该段前后边界: 内部采样直接取端点，否则二分精化翻转区间。
    // 第三参数为该区间内**已验证可见**的端点（vis[] 数组给出）:
    // 入境侧取 runs[best].s, 出境侧取 runs[best].e
    double b0 = (runs[best].s > 0)
        ? refineBoundary(jds[runs[best].s - 1], jds[runs[best].s], jds[runs[best].s])
        : jdAOS;
    double b1 = (jds.size() > 1 && runs[best].e < static_cast<int>(jds.size()) - 1)
        ? refineBoundary(jds[runs[best].e], jds[runs[best].e + 1], jds[runs[best].e])
        : jdLOS;

    ev.hasVisibleArc = true;
    ev.visAosJdMs = jdToIntMs(b0);
    ev.visLosJdMs = jdToIntMs(b1);
}

// ========== 二分精化 AOS ==========
double OrbitCalculator::bisectAOS(PropagateFn &propagate,
                                   const Observer &obs,
                                   double jdBelow, double jdAbove,
                                   const AtomicCancelFlagPtr cancel) const
{
    double maskEl = obs.minElevDeg();
    // 跨窗口过境: 下界处仰角已在掩星角之上（AOS 在窗口起点之前）→ 直接取窗口起点
    if (elevationAt(propagate, obs, jdBelow) >= maskEl)
        return jdBelow;
    double lo = jdBelow, hi = jdAbove;
    for (int i = 0; i < m_cfg.bisectionIters; ++i) {
        if (isCancelled(cancel)) return lo;   // 取消响应: 二分期间也保持敏感
        double mid = (lo + hi) * 0.5;
        double el = elevationAt(propagate, obs, mid);
        if (el == ELEV_FAILURE_SENTINEL) {
            // 传播失败: 向已知 below-horizon 端收缩，保证区间收缩
            hi = mid;
            continue;
        }
        if (el < maskEl) lo = mid;
        else             hi = mid;
    }
    return hi;
}

// ========== 二分精化 LOS ==========
double OrbitCalculator::bisectLOS(PropagateFn &propagate,
                                   const Observer &obs,
                                   double jdAbove, double jdBelow,
                                   const AtomicCancelFlagPtr cancel) const
{
    double maskEl = obs.minElevDeg();
    double lo = jdAbove, hi = jdBelow;
    for (int i = 0; i < m_cfg.bisectionIters; ++i) {
        if (isCancelled(cancel)) return lo;
        double mid = (lo + hi) * 0.5;
        double el = elevationAt(propagate, obs, mid);
        if (el == ELEV_FAILURE_SENTINEL) {
            // 传播失败: 向已知 above-horizon 端收缩，保证区间收缩
            lo = mid;
            continue;
        }
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
                               LookAngle &outLook, GeoPos &outGeo,
                               const AtomicCancelFlagPtr cancel) const
{
    double stepDays = m_cfg.maxElevScanStepSec / 86400.0;
    double bestJD = aosJD;
    double bestEl = ELEV_FAILURE_SENTINEL;
    outJdMs  = 0;       // 取消时保证不输出垃圾值
    outElDeg = ELEV_FAILURE_SENTINEL; // 取消时保证不输出垃圾值

    // 粗扫描（整数索引步进, 避免累加漂移; 点数含端点, 换算见 stepCountInclusive）
    {
        const long long nScan = stepCountInclusive(aosJD, losJD, stepDays);
        for (long long i = 0; i <= nScan; ++i) {
            const double jd = aosJD + static_cast<double>(i) * stepDays;
            if (isCancelled(cancel)) return;
            double el = elevationAt(propagate, obs, jd);
            if (el == ELEV_FAILURE_SENTINEL) continue;   // 失败点不参与峰值竞争
            if (el > bestEl) {
                bestEl = el;
                bestJD = jd;
            }
        }
    }
    // 粗扫峰值时刻: 精化点传播失败时的兜底时刻, 保证 outElDeg 与
    // outLook/outGeo 取自同一时刻（见下方兜底分支）
    const double coarseBestJD = bestJD;

    // 标准三分搜索精化 TCA（单峰函数保证收敛，无平台发散问题）;
    // 半宽取 max(3s, 1.5×粗扫步长)——粗扫最优点与真峰可相距 ±1 步长,
    // 窗口过窄会收敛到次峰; 窗口再钳制到 [aos, los]: 跨窗/截断过境的峰
    // 可能落在窗口边界外, 不钳制会抖出 tcaJd < aosJd 的时序倒挂
    const double delta = std::max(TCA_REFINE_SEC,
                                  1.5 * m_cfg.maxElevScanStepSec) / 86400.0;
    double lo = std::max(bestJD - delta, aosJD);
    double hi = std::min(bestJD + delta, losJD);
    for (int i = 0; i < m_cfg.bisectionIters; ++i) {
        if (isCancelled(cancel)) return;
        double m1 = lo + (hi - lo) / 3.0;
        double m2 = hi - (hi - lo) / 3.0;
        double el1 = elevationAt(propagate, obs, m1);
        double el2 = elevationAt(propagate, obs, m2);
        bool fail1 = (el1 == ELEV_FAILURE_SENTINEL);
        bool fail2 = (el2 == ELEV_FAILURE_SENTINEL);
        if (fail1 && fail2) {
            // 双侧均失败: 向当前最优端收缩，保证窗口收缩
            lo = m1;
            hi = m2;
        } else if (fail1) {
            // m1 失败: 用 m2 收缩
            if (el2 >= bestEl) lo = m1;   // 峰在右侧
            else               hi = m2;   // 峰在左侧或当前位置
        } else if (fail2) {
            // m2 失败: 用 m1 收缩
            if (el1 >= bestEl) hi = m2;   // 峰在左侧
            else               lo = m1;   // 峰在右侧或当前位置
        } else if (el1 >= el2) {
            hi = m2;
        } else {
            lo = m1;
        }
    }

    bestJD = (lo + hi) * 0.5;
    bestJD = std::max(aosJD, std::min(bestJD, losJD));   // 数值误差兜底
    outElDeg = elevationAt(propagate, obs, bestJD);
    // 若精确点传播失败，使用搜索阶段找到的最佳仰角兜底（避免单点故障丢弃有效过境）。
    // 关键: 必须把 bestJD 一并回退到粗扫峰值时刻——否则 outElDeg 来自粗扫峰值、
    // outLook/outGeo 来自精化点, 两个时刻的字段混装会表现为
    // "表格最大仰角 45° 而极坐标 TCA 标注处只有 42°" 的自相矛盾
    if (outElDeg == ELEV_FAILURE_SENTINEL && bestEl > ELEV_FAILURE_SENTINEL) {
        bestJD = coarseBestJD;
        outElDeg = bestEl;
    }
    computeMoment(propagate, obs, bestJD, outLook, outGeo);
    outJdMs = jdToIntMs(bestJD);
}

// ========== 主入口 ==========
std::vector<PassEvent> OrbitCalculator::predict(PropagateFn propagate,
                                                 const Observer &observer,
                                                 double jdStart,
                                                 double hoursAhead,
                                                 const AtomicCancelFlagPtr cancel) const
{
    // 入口防御: 非法配置/非法时间会导致下方主循环 `for(;;)` 永不死去
    // （stepDays<=0 不前进、jdStart=NaN 恒不满足 jd>=jdEnd）→ 拒绝并返回空
    const double stepDays = m_cfg.coarseStepSec / 86400.0;
    
    // 详细的输入验证和错误信息
    if (!(stepDays > 0.0)) {
        throw std::invalid_argument("OrbitCalculator配置错误: 粗搜索步长必须为正数。当前配置: " +
                                   std::to_string(m_cfg.coarseStepSec) + " 秒");
    }
    if (!std::isfinite(jdStart)) {
        throw std::invalid_argument("OrbitCalculator计算错误: 起始儒略日无效。当前值: " +
                                   std::to_string(jdStart));
    }
    if (!std::isfinite(hoursAhead)) {
        throw std::invalid_argument("OrbitCalculator计算错误: 预测时长无效。当前值: " +
                                   std::to_string(hoursAhead) + " 小时");
    }
    if (hoursAhead < 0.0) {
        throw std::invalid_argument("OrbitCalculator配置错误: 预测时长必须为非负数。当前值: " +
                                   std::to_string(hoursAhead) + " 小时");
    }
    // 上限防御: 超长窗口会让 maxSteps 溢出/预分配数百 GB（bad_alloc 未捕获
    // 即进程崩溃）。UI 限 24h, 一年(8760h)上限远高于真实用途
    if (hoursAhead > 8760.0) {
        throw std::invalid_argument("OrbitCalculator配置错误: 预测时长超过一年(8760h)上限。当前值: " +
                                   std::to_string(hoursAhead) + " 小时");
    }
    if (!std::isfinite(observer.minElevDeg())) {
        throw std::invalid_argument("OrbitCalculator配置错误: 掩星角无效。观测站掩星角: " +
                                   std::to_string(observer.minElevDeg()) + "°");
    }
    // 粗搜索步长不超过窗口的 1/4，否则会跳过整个过境
    const double windowSec = hoursAhead * 3600.0;
    if (windowSec > 0.0 && m_cfg.coarseStepSec > windowSec / 4.0) {
        throw std::invalid_argument("OrbitCalculator配置错误: 粗搜索步长超过窗口 1/4。步长: " +
                                   std::to_string(m_cfg.coarseStepSec) + " 秒, 窗口: " +
                                   std::to_string(static_cast<int>(windowSec)) + " 秒");
    }
    // 峰值扫描步长校验: findTCA 以此为 for 步进, ≤0 时条件恒真硬挂死 worker
    //（区别于 coarseStepSec 的"慢", 这是死循环）; 迭代次数下限 1 同理防御
    if (!(m_cfg.maxElevScanStepSec > 0.0) || !std::isfinite(m_cfg.maxElevScanStepSec)) {
        throw std::invalid_argument("OrbitCalculator配置错误: 峰值扫描步长必须为正数。当前值: " +
                                    std::to_string(m_cfg.maxElevScanStepSec) + " 秒");
    }
    if (m_cfg.bisectionIters < 1) {
        throw std::invalid_argument("OrbitCalculator配置错误: 二分迭代次数必须≥1。当前值: " +
                                    std::to_string(m_cfg.bisectionIters));
    }
    
    // 计算结束时间并验证合理性
    const double jdEnd    = jdStart + hoursAhead / 24.0;
    const double maskEl   = observer.minElevDeg();
    
    // 优化：预分配 PassEvent 向量容量，避免频繁重新分配
    // 估算最大可能的过境数量：假设每60分钟一个过境，每个过境持续10分钟，额外预留50%余量
    const double maxEstimatedPasses = std::ceil(hoursAhead * 60.0 / (60.0 + 10.0));
    std::vector<PassEvent> passes;
    {
        size_t cap = maxEstimatedPasses > 0 ? static_cast<size_t>(maxEstimatedPasses) : 10;
        cap += cap / 2;
        passes.reserve(cap);
    }
    
    // 优化：使用移动语义避免 PassEvent 的不必要拷贝
    
    // 添加调试信息（仅在调试模式下）
    #ifdef QT_DEBUG
    qDebug() << QString("OrbitCalculator开始计算: 起始JD=%.6f, 预测时长=%.1f小时, 掩星角=%.1f°")
        .arg(jdStart, 0, 'f', 6)
        .arg(hoursAhead)
        .arg(maskEl);
    #endif

    // 初始状态: 若窗口起点已在掩星角之上，视为过境中（AOS 在窗口前，用窗口起点作下界）
    double el0 = elevationAt(propagate, observer, jdStart);
    bool   inPass     = (el0 >= maskEl);
    double prevEl     = el0;
    double candBelow  = jdStart;   // AOS 候选：仰角 < maskEl 的 JD
    double candAbove  = jdStart;   // AOS 候选：仰角 ≥ maskEl 的 JD

    // ---- 粗搜索 ----
    // 末次采样推进到窗口终点（jdEnd），防窗口末尾一个粗搜步长内的 AOS 漏检;
    // 对完全落在相邻两采样点之间的短过境做中点探测
    auto assemblePass = [&](double jdAOS, double jdLOS,
                            bool truncated = false) -> bool {
        double durSec = (jdLOS - jdAOS) * 86400.0;
        if (durSec < m_cfg.minDurationSec) return false;
        PassEvent ev;
        ev.truncated = truncated;   // 窗口终点截断: LOS 字段为瞬时值非真实出境
        computeMoment(propagate, observer, jdAOS, ev.aosLook, ev.aosSatGeo);
        ev.aosJdMs = jdToIntMs(jdAOS);
        // TCA（findTCA 内部取消时输出已知哨兵值，这里再检查一次取消）
        findTCA(propagate, observer, jdAOS, jdLOS,
                ev.tcaJdMs, ev.maxElevDeg, ev.tcaLook, ev.tcaSatGeo, cancel);
        if (isCancelled(cancel)) return false;
        // 防御: 精化峰值仍低于掩星角（数值异常/传播抖动）时整段丢弃，
        // 防止 findTCA 抖出 -1000 级别的垃圾过境（L-1）
        if (ev.maxElevDeg < maskEl) return false;
        computeMoment(propagate, observer, jdLOS, ev.losLook, ev.losSatGeo);
        ev.losJdMs = jdToIntMs(jdLOS);
        ev.durationSec = durSec;
        collectSamples(propagate, observer, jdAOS, jdLOS, ev, cancel);
        ev.satelliteName = m_satName;
        fillPassDetails(ev, propagate, observer, m_cfg, jdAOS,
                        intMsToJd(ev.tcaJdMs), jdLOS);
        collectVisibleArc(propagate, observer, jdAOS,
                          intMsToJd(ev.losJdMs), ev, cancel);
        passes.push_back(ev);
        return true;
    };

    // 兜底迭代上限: 正常路径由 jd>=jdEnd 退出; 此上限仅防御
    // stepDays 极小值（用户配置 0.001s 级）造成的亿级迭代
    const int64_t maxSteps = static_cast<int64_t>(std::ceil(
        hoursAhead * 86400.0 / std::max(m_cfg.coarseStepSec, 1e-6))) + 2;
    int64_t stepsDone = 0;
    // 采样时刻 = jdStart + 整数索引 × 步长（避免 jd += stepDays 的累积漂移:
    // 24h/15s 窗口累计约 5 ms ≈ 40 m 位置偏差, 会让与 STK 的逐点比对无法收敛）
    for (int64_t i = 0;; ++i) {
        const double jd     = jdStart + static_cast<double>(i) * stepDays;
        const double jdPrev = jdStart + static_cast<double>(i > 0 ? i - 1 : 0) * stepDays;
        if (isCancelled(cancel) || ++stepsDone > maxSteps) return passes;   // 协作式取消

        double el = elevationAt(propagate, observer, jd);
        const bool elFailed = (el == ELEV_FAILURE_SENTINEL);
        // 传播瞬时失败: 无可靠信息, 保持 prevEl 并跳过状态转移/边界检测——
        // 失败点仰角其实可能仍在掩星角之上, 直接当 LOS 会错误分裂或
        // 截断过境; 仅推进时间不丢采样
        if (!elFailed) {
            if (!inPass) {
                // 检测 AOS ▲
                if (prevEl < maskEl && el >= maskEl) {
                    inPass = true;
                    candBelow = jdPrev;
                    candAbove = jd;
                } else if (prevEl < maskEl && i > 0) {
                    // 步内短过境探测(4 等分子步): 原实现只探中点, 完全落在
                    // 一个步长内且不跨中点的 10~15s 级短过境会漏检;
                    // 现对 4 个子区间逐段检测连续 ≥掩星角的子段并逐段装配
                    //
                    // 守卫必须是 i > 0（存在"上一个样本"）: 早前写成
                    // jdPrev >= jdStart, 而 i=0 时 jdPrev == jdStart 也满足该式,
                    // 会在窗口第一个样本处探测 [jdStart-step, jdStart] 这段
                    // **窗口之前**的时间, 可能装配出 AOS 早于窗口起点的过境
                    const double sub = stepDays / 4.0;
                    // 子步网格 pb[0..4] 对应时刻 jd-(4-k)*sub（k=0..4）, 即
                    // [jd-step, jd]。两端是**区间之外**的锚点:
                    // 进入本分支的前提是 prevEl < maskEl（外层第一步判定）
                    // 且 el < maskEl（否则会先走上面的 AOS 分支）, 故 pb[0]/pb[4]
                    // 必然低于掩星角——这是 bisectAOS/bisectLOS 的区间前提。
                    // 显式赋值把该不变量写死（此前是
                    // { prevEl, el, el, el, el } 初始化列表 + 一行口头注释:
                    // t=3 时 bisectLOS 会取到 pb[0], 但"下标0/4 必低于掩星角"
                    // 并无代码保证）。
                    // 不用 assert 兜底: 本工程 release 构建未定义 NDEBUG, 断言
                    // 会直接 abort 整个进程; 且两条二分函数对反常区间自身有兜底
                    //（bisectAOS 在 jdBelow 已高于掩星角时直接取 jdBelow）, 最坏
                    // 结果只是退化为子区间端点, 不会产生窗口外的时刻
                    double pb[5];
                    pb[0] = prevEl;
                    pb[4] = el;
                    for (int k = 1; k <= 3; ++k)
                        pb[k] = elevationAt(propagate, observer, jd - (4 - k) * sub);
                    for (int s = 1; s <= 3; ++s) {
                        if (pb[s] < maskEl) continue;
                        int t = s;
                        while (t < 3 && pb[t + 1] >= maskEl) ++t;
                        // 连续段 [s, t] 两端(t_{s-1}/t_{t+1})均低于掩星角 → 整段可装配
                        assemblePass(
                            bisectAOS(propagate, observer,
                                      jd - (4 - (s - 1)) * sub, jd - (4 - s) * sub,
                                      cancel),
                            bisectLOS(propagate, observer,
                                      jd - (4 - t) * sub, jd - (4 - (t + 1)) * sub,
                                      cancel));
                        s = t;
                    }
                }
            } else {
                // 检测 LOS ▼
                if (prevEl >= maskEl && el < maskEl) {
                    double jdAOS = bisectAOS(propagate, observer,
                                             candBelow, jd, cancel);
                    double jdLOS = bisectLOS(propagate, observer,
                                             candAbove, jd, cancel);
                    assemblePass(jdAOS, jdLOS);
                    inPass = false;
                }
            }
            prevEl = el;
        }
        if (jd >= jdEnd) break;
    }

    // ---- 跨窗口终点处理 ----
    if (inPass) {
        // 过境延伸至窗口终点之外: 按 STK Access 语义在窗口终点 (jdEnd) 截断。
        // LOS 字段/时长均为窗口终点瞬时值, 非真实出境值（truncated=true 标注）。
        double jdAOS = bisectAOS(propagate, observer, candBelow, jdEnd, cancel);
        assemblePass(jdAOS, jdEnd, true);
    }

    // ---- 合并相邻过境 ----
    if (!isCancelled(cancel) && passes.size() > 1 && m_cfg.mergeGapSec > 0.0) {
        const double gapDays = m_cfg.mergeGapSec / 86400.0;
        // 优化：预分配合并后的向量容量
        std::vector<PassEvent> merged;
        merged.reserve(passes.size());  // 最坏情况下不需要合并
        merged.push_back(std::move(passes[0]));
        for (size_t i = 1; i < passes.size(); ++i) {
            double prevLos = intMsToJd(merged.back().losJdMs);
            double curAos  = intMsToJd(passes[i].aosJdMs);
            if (curAos - prevLos < gapDays) {
                // 后段 AOS（ms）: 两段间隙（地平线以下）内的采样点须丢弃,
                // 否则绘出负仰角凹陷并破坏"采样仰角 ≥ 掩星角"的不变量
                const int64_t curAosMs = passes[i].aosJdMs;
                // 截断标记取并集: 任一段被截断则合并结果仍是截断过境
                if (passes[i].truncated) merged.back().truncated = true;
                // 合并：用较晚的 LOS 替换
                if (passes[i].losJdMs > merged.back().losJdMs) {
                    merged.back().losJdMs = passes[i].losJdMs;
                    merged.back().losLook = passes[i].losLook;
                    merged.back().losSatGeo = passes[i].losSatGeo;
                    merged.back().losSunAlt = passes[i].losSunAlt;
                    merged.back().losSatShadow = passes[i].losSatShadow;
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
                }
                // L-2 修复: 弧段采样合并（无论 TCA 取自哪一段都执行），
                // 追加后半段的点并保持时间单调、跳过重叠区与两段间隙
                // （间隙点在 [mergeLosMs, curAosMs) 内, 地平线以下）——
                // 保证 TCA 峰值处的采样点不会从星表中消失
                auto &dst = merged.back().samples;
                const int64_t lastT = dst.empty() ? std::numeric_limits<int64_t>::min()
                                                  : dst.back().jdMs;
                for (const auto &s : passes[i].samples)
                    if (s.jdMs > lastT && s.jdMs >= curAosMs) dst.push_back(s);
            } else {
                merged.push_back(std::move(passes[i]));
            }
        }
        // M-2 修复: 合并可能抬高 maxElevDeg / 替换 TCA，
        // 质量等级须按最终字段重算（公式与 fillPassDetails 保持一致）
        for (auto &ev : merged) {
            if (ev.maxElevDeg >= 50.0)      ev.quality = PassQuality::Excellent;
            else if (ev.maxElevDeg >= 20.0) ev.quality = PassQuality::Good;
            else                            ev.quality = PassQuality::Poor;
            // 可见性同以新 TCA 时刻的光照/太阳高度重推——TCA 可能已换为
            // 后段, 旧分类（前段 TCA）会与展示字段自相矛盾。
            // 前提守卫: 重推依赖 aos/tca/losSunAlt 与 Illum 字段有效——
            // enableSunAnalysis 关闭时它们是结构默认值（0/FullSunlight/1.0）,
            // 无条件重推会用假数据把分类翻转成 Visible, 与未合并段不一致
            if (m_cfg.enableSunAnalysis) {
                bool satLit = (ev.tcaSatShadow != ShadowStatus::InUmbra)
                              && ev.tcaIllum > 0.5;
                if (ev.tcaSunAlt >= m_cfg.darkSkyThresholdDeg)
                    ev.visibility = PassVisibility::Daylight;
                else if (!satLit)
                    ev.visibility = PassVisibility::RadioOnly;
                else
                    ev.visibility = PassVisibility::Visible;
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
