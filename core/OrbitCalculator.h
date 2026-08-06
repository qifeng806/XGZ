#ifndef CORE_ORBITCALCULATOR_H
#define CORE_ORBITCALCULATOR_H
// =============================================================================
// OrbitCalculator.h — 过境预报引擎
// 完整流程: 粗搜(60s步) → 二分精化(18次) → TCA扫描 → 光照/多普勒 → 事件组装
// =============================================================================
#include "models.h"
#include "Observer.h"
#include <atomic>
#include <functional>
#include <optional>
#include <vector>

class OrbitCalculator {
public:
    // 传播函数: JD → TEME 状态（nullopt = 传播失败/衰减）
    using PropagateFn = std::function<std::optional<EciState>(double jd)>;

    struct Config {
        double coarseStepSec      = 60.0;
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
                                   const std::atomic<bool> *cancel = nullptr) const;

    // 卫星名称（用于填充 PassEvent::satelliteName）
    void setSatelliteName(const std::string &name) { m_satName = name; }

private:
    Config m_cfg;
    mutable std::string m_satName;                     // 卫星名称
    mutable const std::atomic<bool> *m_cancel = nullptr; // 协作式取消标志

    bool cancelled() const { return m_cancel && m_cancel->load(); }

    double elevationAt(PropagateFn &propagate,
                       const Observer &obs, double jd) const;

    double bisectAOS(PropagateFn &propagate, const Observer &obs,
                     double jdBelow, double jdAbove) const;
    double bisectLOS(PropagateFn &propagate, const Observer &obs,
                     double jdAbove, double jdBelow) const;

    void findTCA(PropagateFn &propagate, const Observer &obs,
                 double aosJD, double losJD,
                 int64_t &outJdMs, double &outElDeg,
                 LookAngle &outLook, GeoPos &outGeo) const;

    void computeMoment(PropagateFn &propagate, const Observer &obs,
                       double jd, LookAngle &look, GeoPos &geo) const;

    static int64_t jdToIntMs(double jd);
    static double intMsToJd(int64_t ms);
};

#endif // CORE_ORBITCALCULATOR_H
