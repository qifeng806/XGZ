#ifndef CORE_ORBITCALCULATOR_H
#define CORE_ORBITCALCULATOR_H
// =============================================================================
// OrbitCalculator.h — 过境预报引擎
// 完整流程: 粗搜(15s步) → 二分精化(18次) → TCA扫描 → 光照/多普勒 → 事件组装
// =============================================================================
#include "models.h"
#include "Observer.h"
#include "AtomicCancelFlag.h"
#include <atomic>
#include <functional>
#include <optional>
#include <vector>

class OrbitCalculator {
public:
    // 传播函数: JD → TEME 状态（nullopt = 传播失败/衰减）
    using PropagateFn = std::function<std::optional<EciState>(double jd)>;

    struct Config {
        double coarseStepSec      = 15.0;   // 过境检测粗搜步长: 15s 可检出 1~2 分钟短过境
        int    bisectionIters     = 18;
        double maxElevScanStepSec = 3.0;
        double minDurationSec     = 10.0;
        double mergeGapSec        = 60.0;
        bool   enableSunAnalysis   = true;
        bool   enableShadowCheck   = true;
        double darkSkyThresholdDeg = -6.0;
        bool   enableDoppler       = false;
        double carrierFreqMHz      = 437.800;
        bool   enableRefraction    = true;
        bool   enableSamples       = true;   // 采集过境弧段采样点
        double sampleStepSec       = 15.0;   // 弧段采样步长（秒）
        bool   enableMagnitude     = true;   // 星等估算（Vallerie 1963 相位模型）
        double stdMagFallback      = 4.5;    // 无目录匹配时的固有星等（1000 km）
        bool   enableVisArc        = true;   // 可见弧段子区间分析（Heavens-Above 语义）
        Config() = default;
    };

    OrbitCalculator() : m_cfg() {}
    explicit OrbitCalculator(const Config &cfg) : m_cfg(cfg) {}

    // 主入口
    // cancel: 可选的协作式取消标志（工作线程中置位后，预测会在下一个步长提前返回）
    std::vector<PassEvent> predict(PropagateFn propagate,
                                   const Observer &observer,
                                   double jdStart,
                                   double hoursAhead = 24.0,
                                   const AtomicCancelFlagPtr cancel = nullptr) const;

    // 卫星名称（用于填充 PassEvent::satelliteName）
    void setSatelliteName(const std::string &name) { m_satName = name; }

private:
    Config m_cfg;
    mutable std::string m_satName;   // 卫星名称

    // 取消标志沿调用链以参数传递（M 参数化）: 引擎保持无状态,
    // 同一实例可安全并发 predict（使用 shared_ptr 确保生命周期安全）
    static bool isCancelled(const AtomicCancelFlagPtr cancel) {
        return cancel && cancel->isCancelled();
    }
    
    // 安全的取消检查，带有内存屏障
    static bool isCancelledWithMemoryBarrier(const AtomicCancelFlagPtr cancel) {
        if (!cancel) return false;
        bool cancelled = cancel->isCancelled();
        if (cancelled) {
            std::atomic_thread_fence(std::memory_order_acquire);
        }
        return cancelled;
    }

    double elevationAt(PropagateFn &propagate,
                       const Observer &obs, double jd) const;

    double bisectAOS(PropagateFn &propagate, const Observer &obs,
                     double jdBelow, double jdAbove,
                     const AtomicCancelFlagPtr cancel) const;
    double bisectLOS(PropagateFn &propagate, const Observer &obs,
                     double jdAbove, double jdBelow,
                     const AtomicCancelFlagPtr cancel) const;

    void findTCA(PropagateFn &propagate, const Observer &obs,
                 double aosJD, double losJD,
                 int64_t &outJdMs, double &outElDeg,
                 LookAngle &outLook, GeoPos &outGeo,
                 const AtomicCancelFlagPtr cancel) const;

    void computeMoment(PropagateFn &propagate, const Observer &obs,
                       double jd, LookAngle &look, GeoPos &geo) const;

    // 在 [AOS, LOS] 内等步长采集弧段采样点（填 PassEvent::samples，
    // enableDoppler 时同时填充各采样点多普勒）
    void collectSamples(PropagateFn &propagate, const Observer &obs,
                        double jdAOS, double jdLOS, PassEvent &ev,
                        const AtomicCancelFlagPtr cancel) const;

    // 可见弧段子区间: 在 [AOS, LOS] 内扫描"暗天 + 被照亮"的最大连续区间，
    // 边界做二分精化（填 visAosJdMs/visLosJdMs/hasVisibleArc）
    void collectVisibleArc(PropagateFn &propagate, const Observer &obs,
                            double jdAOS, double jdLOS, PassEvent &ev,
                            const AtomicCancelFlagPtr cancel) const;

    static int64_t jdToIntMs(double jd);
    static double intMsToJd(int64_t ms);
};

#endif // CORE_ORBITCALCULATOR_H
