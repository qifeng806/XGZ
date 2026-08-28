// =============================================================================
// OrbitCalculator.cpp — 过境预报引擎完整实现
// =============================================================================
#include "OrbitCalculator.h"
#include "CoordTransform.h"
#include "SunPosition.h"
#include "EarthShadow.h"
#include "DopplerCalc.h"
#include "VisualMagnitude.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
    constexpr double EXTEND_MIN   = 60.0;      // 跨窗口过境延长搜索（分钟）
    constexpr double TCA_REFINE_SEC = 3.0;     // TCA 三分搜索初始半宽（秒）
    constexpr double SEC_PER_DAY  = 86400.0;
    // 传播失败哨兵: 真实仰角物理下限为 -90°, 该值不可能由观测产生。
    // 调用方必须用它区分"传播失败"与"真实低于掩星角"——把失败点当真实
    // LOS/AOS 会把过境错误分裂或截断（瞬态失败时仰角其实仍在掩星角之上）
    constexpr double ELEV_FAILURE_SENTINEL = -1000.0;
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
        
        // 仅在需要时才进行轨道传播
        if (cfg.enableSunAnalysis && cfg.enableShadowCheck) {
            // 需要所有三个时刻的数据
            eciA = propagate(jdAOS);
            eciT = propagate(jdTCA);
            eciL = propagate(jdLOS);
        } else if (cfg.enableShadowCheck || cfg.enableMagnitude || cfg.enableDoppler) {
            // 只需要 TCA 时刻的数据
            eciT = propagate(jdTCA);
        }
        
        // 光照分析
        if (cfg.enableSunAnalysis) {
            // 优化：缓存太阳位置计算结果
            const auto sunA = SunPositionCalc::compute(jdAOS, observer.latDeg(), observer.lonDeg());
            const auto sunT = SunPositionCalc::compute(jdTCA, observer.latDeg(), observer.lonDeg());
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

            double bestSunAlt = std::max({ev.aosSunAlt, ev.tcaSunAlt, ev.losSunAlt});
            // 光照判定以 TCA（用户实际观测时刻）为准 —— 与 Heavens-Above/Orbis
            // 的「峰值点燃亮」语义一致：仅 AOS/LOS 照亮、TCA 却在地影内的过境
            // 不标为可见（否则用户按 TCA 时刻观测看不到卫星）
            // 暗天阈值按站点海拔的地平线倾角修正（见 horizonDipDeg）
            bool satLit = (ev.tcaSatShadow != ShadowStatus::InUmbra) && ev.tcaIllum > 0.5;
            const double dark = cfg.darkSkyThresholdDeg - horizonDipDeg(observer.altKm());
            if (bestSunAlt < dark && satLit)
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
                    auto sunDirE = CoordTransform::eciDirToEcef(sunT->dirECI, jdTCA);
                    std::array<double, 3> sunPosE;
                    for (int k = 0; k < 3; ++k)
                        sunPosE[k] = sunDirE[k] * (sunT->distanceAU * EarthShadow::AU_KM);
                    const double phase = VisualMagnitudeCalc::phaseAngleRad(satE, sunPosE,
                                                                      observer.ecef());
                    ev.magAtTca = VisualMagnitudeCalc::visualMagnitude(
                        VisualMagnitudeCalc::standardMagnitude(ev.satelliteName, cfg.stdMagFallback),
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
    } catch (const std::exception &e) {
        // 异常情况下设置默认值，避免数据损坏
        ev.visibility = PassVisibility::Daylight;
        ev.quality = PassQuality::Poor;
        ev.passType = 0;
        
        // 记录错误但不中断计算
        // 在实际应用中，这里可以添加日志记录
        // std::cerr << "fillPassDetails 异常: " << e.what() << std::endl;
    }
}

int64_t OrbitCalculator::jdToIntMs(double jd) {
    // 检查输入值的有效性
    if (!std::isfinite(jd)) {
        return 0;
    }
    
    // 检查极端值，防止溢出
    // int64_t 的范围大约是 ±9.2e18，86400000.0 * jd 必须在这个范围内
    const double maxSafeJd = 9.2e18 / 86400000.0;
    const double minSafeJd = -9.2e18 / 86400000.0;
    
    if (jd > maxSafeJd) {
        return std::numeric_limits<int64_t>::max();
    }
    if (jd < minSafeJd) {
        return std::numeric_limits<int64_t>::min();
    }
    
    // 使用更精确的转换算法
    // 将 jd 分解为整数和小数部分，分别处理以减少精度损失
    int64_t jdInt = static_cast<int64_t>(std::floor(jd));
    double jdFrac = jd - jdInt;
    
    // 计算整数部分和小数部分的毫秒数
    int64_t intMs = jdInt * 86400000LL;
    int64_t fracMs = static_cast<int64_t>(std::round(jdFrac * 86400000.0));
    
    // 处理四舍五入导致的进位
    if (fracMs >= 86400000LL) {
        intMs += 86400000LL;
        fracMs -= 86400000LL;
    } else if (fracMs < 0) {
        intMs -= 86400000LL;
        fracMs += 86400000LL;
    }
    
    // 注: 此处 fracMs 已归一化到 [0, 86400000), 无需再做符号修正
    //（原「最终结果检查」分支是数值恒等变换, 恒不改变结果, 已删除）
    
    return intMs + fracMs;
}
double OrbitCalculator::intMsToJd(int64_t ms) {
    // 检查极端值
    if (ms == std::numeric_limits<int64_t>::max()) {
        return static_cast<double>(std::numeric_limits<int64_t>::max()) / 86400000.0;
    }
    if (ms == std::numeric_limits<int64_t>::min()) {
        return static_cast<double>(std::numeric_limits<int64_t>::min()) / 86400000.0;
    }
    
    // 使用更精确的转换算法
    // 将毫秒数分解为整天和剩余毫秒数
    const int64_t msPerDay = 86400000LL;
    int64_t days = ms / msPerDay;
    int64_t remainingMs = ms % msPerDay;
    
    // 处理负数情况
    if (ms < 0 && remainingMs != 0) {
        days--;
        remainingMs += msPerDay;
    }
    
    // 计算儒略日
    double jd = static_cast<double>(days) + static_cast<double>(remainingMs) / 86400000.0;
    
    // 确保结果在合理范围内
    if (!std::isfinite(jd)) {
        return 0.0;
    }
    
    return jd;
}

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
        
        const double step = m_cfg.sampleStepSec / 86400.0;
        ev.samples.clear();
        // 防御: 调用链保证 jdLOS > jdAOS, 但数值抖动/上游配置错误时负数会
        // 在 size_t 转换后变成天文数字触发 bad_alloc
        const double n = std::max(0.0, (jdLOS - jdAOS) / step);
        
        // 防止分配过多内存
        if (n > 100000.0) {  // 限制最大采样点数
            return;
        }
        
        // 优化：更精确的预分配策略
        size_t estimatedSamples = static_cast<size_t>(n) + 2;
        // 如果启用了多普勒计算，可能需要更多空间
        if (m_cfg.enableDoppler) {
            estimatedSamples = static_cast<size_t>(n * 1.1) + 2;  // 增加10%缓冲
        }
        ev.samples.reserve(estimatedSamples);
        
        // 优化：预留额外容量以避免重新分配
        ev.samples.reserve(ev.samples.capacity() + ev.samples.capacity() / 4);  // 额外预留25%空间
        
        // 传播一次完成全部字段（含多普勒），避免对同一时刻二次传播
        for (double jd = jdAOS; jd <= jdLOS + 1e-12; jd += step) {
            if (isCancelled(cancel)) return;
            
            try {
                auto eciOpt = propagate(jd);
                LookAngle look;
                GeoPos geo;
                double dopplerKHz = 0.0;
                
                if (eciOpt) {
                    auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
                    look = obs.lookAngle(ecef);
                    geo = obs.subSatPoint(ecef);
                    
                    if (m_cfg.enableDoppler) {
                        dopplerKHz = DopplerCalc::compute(eciOpt->posKm, eciOpt->velKms, jd,
                                                          obs.ecef(), m_cfg.carrierFreqMHz);
                    }
                } else {
                    look = LookAngle(-1000.0, 0.0, 0.0);   // 传播失败占位（与 computeMoment 一致）
                    geo = GeoPos(0.0, 0.0, 0.0);
                }
                
                // 检查采样数据有效性
                if (std::isfinite(look.elevDeg) && std::isfinite(look.azDeg) && 
                    std::isfinite(look.rangeKm) && std::isfinite(geo.latDeg) && 
                    std::isfinite(geo.lonDeg) && std::isfinite(geo.altKm)) {
                    ev.samples.push_back({ jdToIntMs(jd), look, geo, dopplerKHz });
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

    const double step  = m_cfg.sampleStepSec / 86400.0;
    const double dark  = m_cfg.darkSkyThresholdDeg - horizonDipDeg(obs.altKm());

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

    // 二分精化状态翻转边界: [lo, hi] 端点可见性已知不同，收敛到区间内的翻转点。
    // 不变式: visibleAt(lo) == A, visibleAt(hi) == B（A ≠ B），收敛到翻转位置。
    // 防御: 若区间内存在多次翻转（半影边缘抖动/阈值边界叠加的罕见情况），
    // 二分会收敛到任意一个翻转点而非首/末边界——每轮重查端点, 一旦端点
    // 可见性相同即退化为返回区间端点（真实可见段的成员），不做错误精化
    auto refineBoundary = [&](double jdLo, double jdHi) -> double {
        for (int i = 0; i < m_cfg.bisectionIters; ++i) {
            if (isCancelled(cancel)) return jdLo;
            bool vLo = visibleAt(jdLo);
            bool vHi = visibleAt(jdHi);
            if (vLo == vHi) return jdLo;   // 多翻转: 不精化, 返回端点
            double mid = (jdLo + jdHi) * 0.5;
            if (visibleAt(mid)) jdHi = mid;
            else                jdLo = mid;
        }
        return (jdLo + jdHi) * 0.5;
    };

    // 采样网格: 等步长 + 强制纳入 TCA（保证相邻采样状态不同处边界精化正确，
    // 且 TCA 时刻可见性分类与弧段包含关系严格一致）
    std::vector<double> jds;
    const double nStep = (jdLOS - jdAOS) / step;
    if (nStep > 100000.0) {
        // 与 collectSamples 同款上限（GEO 长过境 + 极小步长下逐点
        // visibleAt 即 SunPosition+propagate+umbraFactor, 无界耗时）
        return;
    }
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

    // 多段时优先取包含 TCA 的连续段（M-1: 可见弧段与可见性分类同以 TCA
    // 时刻为准，避免"弧段未含 TCA"与"标为可见"互相矛盾）；
    // 仅当 TCA 不在任何可见段内（理论矛盾，防御）时退化为取最长段
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
        if (el == ELEV_FAILURE_SENTINEL) continue;   // 失败点无可靠仰角: 不缩窗
        if (el < maskEl) lo = mid;
        else             hi = mid;
    }
    return lo;
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
        if (el == ELEV_FAILURE_SENTINEL) continue;   // 失败点无可靠仰角: 不缩窗
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

    // 粗扫描
    for (double jd = aosJD; jd <= losJD; jd += stepDays) {
        if (isCancelled(cancel)) return;
        double el = elevationAt(propagate, obs, jd);
        if (el == ELEV_FAILURE_SENTINEL) continue;   // 失败点不参与峰值竞争
        if (el > bestEl) {
            bestEl = el;
            bestJD = jd;
        }
    }

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
        if (el1 == ELEV_FAILURE_SENTINEL || el2 == ELEV_FAILURE_SENTINEL)
            continue;   // 任一点失败: 不缩窗（避免把失败点当真实峰值）
        if (el1 >= el2) hi = m2;
        else            lo = m1;
    }

    bestJD = (lo + hi) * 0.5;
    bestJD = std::max(aosJD, std::min(bestJD, losJD));   // 数值误差兜底
    outElDeg = elevationAt(propagate, obs, bestJD);
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
    // 估算最大可能的过境数量：假设每60分钟一个过境，每个过境持续10分钟
    const double maxEstimatedPasses = std::ceil(hoursAhead * 60.0 / (60.0 + 10.0));
    std::vector<PassEvent> passes;
    passes.reserve(maxEstimatedPasses > 0 ? static_cast<size_t>(maxEstimatedPasses) : 10);
    
    // 优化：使用移动语义避免 PassEvent 的不必要拷贝
    passes.reserve(passes.capacity() + passes.capacity() / 2);  // 额外预留50%空间
    
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
        ev.truncated = truncated;   // EXTEND 截断: LOS 字段为瞬时值非真实出境
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
    double lastGridJd = jdStart;   // 最后一个粗搜网格点（EXTEND 续扫起点）
    for (double jd = jdStart;; jd += stepDays) {
        if (isCancelled(cancel) || ++stepsDone > maxSteps) return passes;   // 协作式取消

        double el = elevationAt(propagate, observer, jd);
        const bool elFailed = (el == ELEV_FAILURE_SENTINEL);
        // 传播瞬时失败: 无可靠信息, 保持 prevEl 并跳过状态转移/边界检测——
        // 失败点仰角其实可能仍在掩星角之上, 直接当 LOS 会错误分裂或
        // 截断过境（EXTEND 假截断亦然）; 仅推进时间不丢采样
        if (!elFailed) {
            if (!inPass) {
                // 检测 AOS ▲
                if (prevEl < maskEl && el >= maskEl) {
                    inPass = true;
                    candBelow = jd - stepDays;
                    candAbove = jd;
                } else if (prevEl < maskEl && jd - stepDays >= jdStart) {
                    // 步内短过境探测(4 等分子步): 原实现只探中点, 完全落在
                    // 一个步长内且不跨中点的 10~15s 级短过境会漏检;
                    // 现对 4 个子区间逐段检测连续 ≥掩星角的子段并逐段装配
                    const double sub = stepDays / 4.0;
                    double pb[5] = { prevEl, el, el, el, el }; // 下标0/4 必低于掩星角
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
        if (jd >= jdEnd) { lastGridJd = jd; break; }
    }

    // ---- 跨 24h 边界处理 ----
    if (inPass) {
        // 从最后一个粗搜网格点续扫而非窗口终点: prevEl 属于该网格点——
        // 若从 jdEnd 重扫, 首个比较是「晚时刻的 prevEl vs 早时刻的 el」,
        // 真实 LOS 落在 [jdEnd, lastGridJd] 网格缝内时二分括号不含翻转点,
        // LOS 收敛偏早最多一个粗搜步长
        double jdExtend = jdEnd + EXTEND_MIN / 1440.0;
        double jdLos = -1.0;
        for (double jd = lastGridJd; jd < jdExtend; jd += stepDays) {
            if (isCancelled(cancel)) return passes;

            double el = elevationAt(propagate, observer, jd);
            if (el == ELEV_FAILURE_SENTINEL) continue;   // 失败点不触发 LOS 判定
            if (prevEl >= maskEl && el < maskEl) {
                jdLos = bisectLOS(propagate, observer, candAbove, jd, cancel);
                break;
            }
            prevEl = el;
        }

        // 延长窗口结束仍未见 LOS（高轨/同步卫星长过境）:
        // 以窗口终点为 LOS 截断收尾，避免过境整体丢失——标记 truncated,
        // 其 LOS 字段/时长均为截断值而非真实出境值（UI 注明）
        const bool truncated = (jdLos < 0.0);
        if (jdLos < 0.0)
            jdLos = jdExtend;

        double jdAOS = bisectAOS(propagate, observer, candBelow, jdLos, cancel);
        assemblePass(jdAOS, jdLos, truncated);
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
                // 截断标记取并集: 任一段被 EXTEND 截断则合并结果仍是截断过境
                if (passes[i].truncated) merged.back().truncated = true;
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
        // M-2 修复: 合并可能抬高 maxElevDeg / 替换 TCA（连同 azDiff 一起变动），
        // 质量等级与过境类型须按最终字段重算（公式与 fillPassDetails 保持一致）
        for (auto &ev : merged) {
            if (ev.maxElevDeg >= 50.0)      ev.quality = PassQuality::Excellent;
            else if (ev.maxElevDeg >= 20.0) ev.quality = PassQuality::Good;
            else                            ev.quality = PassQuality::Poor;
            double azDiff = ev.tcaLook.azDeg - ev.aosLook.azDeg;
            if (azDiff > 180.0) azDiff -= 360.0;
            if (azDiff < -180.0) azDiff += 360.0;
            ev.passType = (azDiff > 0.0) ? 0 : 1;
            // 可见性同以新 TCA 时刻的光照/太阳高度重推——TCA 可能已换为
            // 后段, 旧分类（前段 TCA）会与展示字段自相矛盾。
            // 前提守卫: 重推依赖 aos/tca/losSunAlt 与 Illum 字段有效——
            // enableSunAnalysis 关闭时它们是结构默认值（0/FullSunlight/1.0）,
            // 无条件重推会用假数据把分类翻转成 Visible, 与未合并段不一致
            if (m_cfg.enableSunAnalysis && m_cfg.enableShadowCheck) {
                double bestSunAlt = std::max({ev.aosSunAlt, ev.tcaSunAlt, ev.losSunAlt});
                bool satLit = (ev.tcaSatShadow != ShadowStatus::InUmbra)
                              && ev.tcaIllum > 0.5;
                const double dark = m_cfg.darkSkyThresholdDeg
                                    - horizonDipDeg(observer.altKm());
                if (bestSunAlt < dark && satLit)
                    ev.visibility = PassVisibility::Visible;
                else if (bestSunAlt < m_cfg.darkSkyThresholdDeg)
                    ev.visibility = PassVisibility::RadioOnly;
                else
                    ev.visibility = PassVisibility::Daylight;
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
