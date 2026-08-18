// =============================================================================
// tst_orbitcalc.cpp — 过境预报引擎单元测试
// 合成圆形轨道（解析开普勒传播）验证:
//   AOS ≤ TCA ≤ LOS | AOS/LOS 掩星角精确性 | 时长合理性 | 取消响应
//   合并 | 最短时长过滤 | 窗口起点已在过境中 | 无过境情形
// =============================================================================
#include <QtTest/QtTest>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <limits>
#include "core/OrbitCalculator.h"
#include "core/CoordTransform.h"
#include "testutils.h"
#include "tst_runners.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

constexpr double MU      = 398600.8;      // km³/s²
constexpr double RADIUS  = 6378.137;      // 赤道半径 km
constexpr double ALT     = 400.0;         // 轨道高度 km
constexpr double A       = RADIUS + ALT;  // 半长轴
constexpr double N       = std::sqrt(MU / (A * A * A));   // 平均角速度 rad/s
constexpr double JD0     = 2460390.0;     // 2024-03-20 12:00 UT

// 合成开普勒传播器: 圆轨道, 倾角 inclDeg, M0=0（t=0 位于升交点/赤道面 +X）
OrbitCalculator::PropagateFn keplerFn(double inclDeg, double m0Deg = 0.0)
{
    const double inc = inclDeg * M_PI / 180.0;
    const double m0  = m0Deg * M_PI / 180.0;
    return [inc, m0](double jd) -> std::optional<EciState> {
        const double u = m0 + N * (jd - JD0) * 86400.0;
        const double cu = std::cos(u), su = std::sin(u);
        EciState st;
        st.posKm  = { A * cu, A * su * std::cos(inc), A * su * std::sin(inc) };
        st.velKms = { -A * N * su, A * N * cu * std::cos(inc), A * N * cu * std::sin(inc) };
        return st;
    };
}

double elevationAt(const OrbitCalculator::PropagateFn &fn,
                   const Observer &obs, double jd)
{
    auto st = fn(jd);
    if (!st) return -1000.0;
    // 与引擎一致: 坐标旋转用 UTC 儒略日（UT1−UTC ≤ 0.9s, 直接近似;
    // 切不可加 ΔT，否则地固系旋转系统性错位 ~69s → 峰值偏差达数度）
    return obs.lookAngle(CoordTransform::eciToEcef(st->posKm, jd)).elevDeg;
}

// 断言浮点有限
void assertFinite(const PassEvent &ev)
{
    QVERIFY(std::isfinite(static_cast<double>(ev.aosJdMs)));
    QVERIFY(std::isfinite(static_cast<double>(ev.tcaJdMs)));
    QVERIFY(std::isfinite(static_cast<double>(ev.losJdMs)));
    QVERIFY(std::isfinite(ev.maxElevDeg));
    QVERIFY(std::isfinite(ev.durationSec));
    QVERIFY(std::isfinite(ev.aosSunAlt) && std::isfinite(ev.tcaSunAlt)
            && std::isfinite(ev.losSunAlt));
    QVERIFY(ev.maxElevDeg >= -90.0 && ev.maxElevDeg <= 90.0);
    QVERIFY(ev.aosLook.elevDeg >= -90.0 && ev.aosLook.elevDeg <= 90.0);
    QVERIFY(ev.losLook.elevDeg >= -90.0 && ev.losLook.elevDeg <= 90.0);
}

} // namespace

class TstOrbitCalc : public QObject {
    Q_OBJECT
private slots:
    void predictFindsPasses();
    void aosLosAtMaskElevation();
    void tcaIsTrueMaximum();
    void startsAboveMaskCaptured();
    void noPassWhenSatelliteBelowHorizon();
    void propagationFailureTolerated();
    void cooperativeCancellation();
    void mergeAndMinDuration();
    void samplesCoverPass();
    void visibleArcConsistentWithVisibility();
    void magnitudeEstimateSanity();
    void shortPassWithinStepDetected();
    void truncatedPassAtWindowEnd();
};

void TstOrbitCalc::predictFindsPasses()
{
    // 60°N 观测站 + 极地轨道: 地面轨迹每天多次进入可见区
    Observer obs(60.0, 0.0, 0.0, 10.0);
    OrbitCalculator calc;
    calc.setSatelliteName("SYNTH-LEO");

    auto passes = calc.predict(keplerFn(90.0), obs, JD0, 24.0);
    QVERIFY2(passes.size() >= 4, qPrintable(QString("passes=%1").arg(passes.size())));

    for (const auto &ev : passes) {
        assertFinite(ev);
        QVERIFY(ev.aosJdMs < ev.tcaJdMs);
        QVERIFY(ev.tcaJdMs < ev.losJdMs);
        QVERIFY(ev.maxElevDeg >= 10.0);
        QVERIFY(ev.durationSec >= 9.5 && ev.durationSec < 1200.0);
        QVERIFY(ev.passType == 0 || ev.passType == 1);
        QCOMPARE(ev.satelliteName, std::string("SYNTH-LEO"));
        // 可见性枚举合法
        QVERIFY(ev.visibility == PassVisibility::Daylight
                || ev.visibility == PassVisibility::Visible
                || ev.visibility == PassVisibility::RadioOnly);
        // 大气折射修正: 修正量恒非负（[0,1]° 线性过渡带内同样成立）
        QVERIFY(ev.aosElRefracted >= ev.aosLook.elevDeg);
        // 时长 = LOS − AOS（毫秒一致性, ±2s）
        const double dur = static_cast<double>(ev.losJdMs - ev.aosJdMs) / 1000.0;
        QVERIFY(std::abs(dur - ev.durationSec) < 2.0);
    }
}

void TstOrbitCalc::aosLosAtMaskElevation()
{
    Observer obs(60.0, 0.0, 0.0, 10.0);
    OrbitCalculator calc;
    auto passes = calc.predict(keplerFn(90.0), obs, JD0, 24.0);
    QVERIFY(passes.size() >= 4);

    for (const auto &ev : passes) {
        // 二分精化后 AOS/LOS 仰角应精确贴住掩星角（合成函数光滑, 容差 0.05°）
        QVERIFY(std::abs(ev.aosLook.elevDeg - 10.0) < 0.05);
        QVERIFY(std::abs(ev.losLook.elevDeg - 10.0) < 0.05);
        // 星下点高度一致性: 合成轨道是半径 A 的球面，因此由报出的
        // (φ, λ, h) 反算的 ECEF 点必须落在该球面上: |geodeticToEcef| = A
        const double orbitRadiusKm = A;
        auto checkRadius = [orbitRadiusKm](const GeoPos &g, const char *tag) {
            auto ec = CoordTransform::geodeticToEcef(g);
            double mag = std::sqrt(ec[0] * ec[0] + ec[1] * ec[1] + ec[2] * ec[2]);
            QVERIFY2(std::abs(mag - orbitRadiusKm) < 1e-3,
                     qPrintable(QString("%1: |ECEF|=%2 期望 %3")
                                .arg(tag).arg(mag, 0, 'f', 4).arg(orbitRadiusKm, 0, 'f', 4)));
        };
        checkRadius(ev.aosSatGeo, "aos");
        checkRadius(ev.tcaSatGeo, "tca");
        checkRadius(ev.losSatGeo, "los");
        QVERIFY(ev.aosSatGeo.altKm > 0.0 && ev.aosSatGeo.altKm < A);
    }
}

void TstOrbitCalc::tcaIsTrueMaximum()
{
    Observer obs(60.0, 0.0, 0.0, 10.0);
    OrbitCalculator calc;
    auto passes = calc.predict(keplerFn(90.0), obs, JD0, 24.0);
    QVERIFY(passes.size() >= 4);

    for (const auto &ev : passes) {
        // 独立精细扫描 [AOS, LOS] 找最大仰角, 与引擎 TCA 对比。
        // 引擎用三分搜索细化到真实峰顶, 稀疏网格（如 5s）会钝化峰顶
        //（近天顶过境 5s 偏移可达 2°+）, 故独立扫描用 0.2s 步进
        double best = -1000.0;
        const double jdA = static_cast<double>(ev.aosJdMs) / 86400000.0;
        const double jdL = static_cast<double>(ev.losJdMs) / 86400000.0;
        auto fn = keplerFn(90.0);
        for (double jd = jdA; jd <= jdL; jd += 0.2 / 86400.0)
            best = std::max(best, elevationAt(fn, obs, jd));
        QVERIFY2(std::abs(best - ev.maxElevDeg) < 0.05,
                 qPrintable(QString("TCA=%1 scan=%2")
                            .arg(ev.maxElevDeg, 0, 'f', 3)
                            .arg(best, 0, 'f', 3)));
        // TCA 处仰角即最大仰角
        QVERIFY(std::abs(ev.tcaLook.elevDeg - ev.maxElevDeg) < 1e-6);
        // 质量分级一致性
        PassQuality expect = (ev.maxElevDeg >= 50.0) ? PassQuality::Excellent
                            : (ev.maxElevDeg >= 20.0) ? PassQuality::Good
                            : PassQuality::Poor;
        QCOMPARE(ev.quality, expect);
    }
}

void TstOrbitCalc::startsAboveMaskCaptured()
{
    Observer obs(60.0, 0.0, 0.0, 10.0);
    auto fn = keplerFn(90.0);

    // 找第一个过境: 首个仰角 ≥ 掩星角的采样点（60s 粗扫）
    double t0 = -1.0;
    for (double jd = JD0; jd < JD0 + 0.1; jd += 60.0 / 86400.0) {
        if (elevationAt(fn, obs, jd) >= 10.0) { t0 = jd; break; }
    }
    QVERIFY(t0 > 0.0);

    // 窗口起点取在过境开始后 30 秒 → 起点已在掩星角之上
    const double jdStart = t0 + 30.0 / 86400.0;
    QVERIFY(elevationAt(fn, obs, jdStart) >= 10.0);

    OrbitCalculator calc;
    auto passes = calc.predict(fn, obs, jdStart, 24.0);
    QVERIFY(passes.size() >= 4);

    // 必须存在一个 AOS ≤ 窗口起点 < TCA 的过境（进行中的过境被捕获）
    bool found = false;
    const int64_t startMs = jdToMs(jdStart);
    for (const auto &ev : passes) {
        if (ev.aosJdMs <= startMs && startMs < ev.tcaJdMs) {
            found = true;
            break;
        }
    }
    QVERIFY2(found, "窗口起点已过境中的情况未被正确处理");
}

void TstOrbitCalc::noPassWhenSatelliteBelowHorizon()
{
    // 赤道轨道 + 北极观测站: 卫星永远在地平线以下
    Observer obs(89.9, 0.0, 0.0, 10.0);
    OrbitCalculator calc;
    auto passes = calc.predict(keplerFn(0.0), obs, JD0, 24.0);
    QVERIFY(passes.empty());
}

void TstOrbitCalc::propagationFailureTolerated()
{
    Observer obs(60.0, 0.0, 0.0, 10.0);
    OrbitCalculator calc;

    // 传播函数始终失败 → 不崩溃, 返回空
    auto failFn = [](double) -> std::optional<EciState> { return std::nullopt; };
    QVERIFY(calc.predict(failFn, obs, JD0, 24.0).empty());

    // 部分失败（确定性注入）: 仅第 5 次调用失败一次（不落在关键采样的
    // 固定时刻, 与调用顺序解耦不了但完全确定, 无随机性）→ 仍能正常预报
    int calls = 0;
    auto flaky = [&calls](double jd) -> std::optional<EciState> {
        if (++calls == 5) return std::nullopt;
        return keplerFn(90.0)(jd);
    };
    auto passes = calc.predict(flaky, obs, JD0, 24.0);
    QVERIFY(passes.size() >= 4);
}

void TstOrbitCalc::cooperativeCancellation()
{
    Observer obs(60.0, 0.0, 0.0, 10.0);
    auto cancel = makeAtomicCancelFlag();

    // 立即取消 → 空结果且快速返回
    cancel->set(true);
    OrbitCalculator calc;
    auto early = calc.predict(keplerFn(90.0), obs, JD0, 24.0, cancel);
    QVERIFY(early.empty());

    // 基线: 未取消时的完整扫描调用量（60s 粗扫 + 逐过境细化）——
    // 先测基线再测取消，用相对比例断言，避免把实现细节（绝对次数）烤进测试
    int fullCalls = 0;
    auto countingFull = [&fullCalls](double jd) -> std::optional<EciState> {
        ++fullCalls;
        return keplerFn(90.0)(jd);
    };
    auto never = makeAtomicCancelFlag();
    auto full = calc.predict(countingFull, obs, JD0, 24.0, never);
    QVERIFY2(fullCalls > 1000 && !full.empty(),
             qPrintable(QString("基线扫描异常: 调用=%1 过境=%2")
                        .arg(fullCalls).arg(full.size())));

    // 中途取消: 传播函数内计数, 第 500 次调用后置位 → 提前返回
    cancel->set(false);
    int calls = 0;
    auto counting = [&calls, &cancel](double jd) -> std::optional<EciState> {
        if (++calls >= 500) cancel->set(true);
        return keplerFn(90.0)(jd);
    };
    auto partial = calc.predict(counting, obs, JD0, 24.0, cancel);
    QVERIFY2(calls >= 500,
             qPrintable(QString("取消标志未生效: calls=%1").arg(calls)));
    QVERIFY2(calls < fullCalls / 5,
             qPrintable(QString("取消响应不及时: calls=%1/基线=%2")
                        .arg(calls).arg(fullCalls)));
}

void TstOrbitCalc::mergeAndMinDuration()
{
    Observer obs(60.0, 0.0, 0.0, 10.0);

    // 不合并
    OrbitCalculator::Config cfgNoMerge;
    cfgNoMerge.mergeGapSec = 0.0;
    OrbitCalculator calcA(cfgNoMerge);
    auto raw = calcA.predict(keplerFn(90.0), obs, JD0, 24.0);

    // 大合并窗口（相邻过境间隔 ≈ 92 min → 7200 s 全部合并）
    OrbitCalculator::Config cfgMerge;
    cfgMerge.mergeGapSec = 7200.0;
    OrbitCalculator calcB(cfgMerge);
    auto merged = calcB.predict(keplerFn(90.0), obs, JD0, 24.0);
    QVERIFY2(merged.size() < raw.size() && !merged.empty(),
             qPrintable(QString("合并后=%1 未合并=%2")
                        .arg(merged.size()).arg(raw.size())));
    QVERIFY(merged[0].losJdMs > merged[0].aosJdMs);

    // 合并语义逐字段验证: 按合并间隙把 raw 重分组, 逐组合并结果
    {
        struct RawGroup { size_t first, last; };
        std::vector<RawGroup> rawGroups;
        for (size_t i = 0; i < raw.size();) {
            size_t j = i;
            while (j + 1 < raw.size()
                   && raw[j + 1].aosJdMs - raw[j].losJdMs < 7200000)   // 7200 s
                ++j;
            rawGroups.push_back({ i, j });
            i = j + 1;
        }
        QVERIFY2(merged.size() == rawGroups.size(),
                 qPrintable(QString("合并组数不符: 引擎=%1 期望=%2")
                            .arg(merged.size()).arg(rawGroups.size())));

        for (size_t g = 0; g < rawGroups.size(); ++g) {
            const auto &m  = merged[g];
            const size_t a = rawGroups[g].first;
            const size_t b = rawGroups[g].last;
            QVERIFY(m.aosJdMs == raw[a].aosJdMs);        // AOS 取首段
            QVERIFY(m.losJdMs == raw[b].losJdMs);        // LOS 取末段
            const double unionDur = static_cast<double>(raw[b].losJdMs
                                                        - raw[a].aosJdMs) / 1000.0;
            QVERIFY(std::abs(m.durationSec - unionDur) < 2.0);   // 时长为并集总长
            if (m.hasVisibleArc) {
                // 可见弧段并集 = min(组内各段 visAos) ~ max(组内各段 visLos)
                int64_t vMin = std::numeric_limits<int64_t>::max();
                int64_t vMax = 0;
                for (size_t r = a; r <= b; ++r) {
                    if (!raw[r].hasVisibleArc) continue;
                    vMin = std::min(vMin, raw[r].visAosJdMs);
                    vMax = std::max(vMax, raw[r].visLosJdMs);
                }
                QVERIFY(vMax > vMin);                    // 组内必有一段有弧
                QVERIFY(m.visAosJdMs == vMin);
                QVERIFY(m.visLosJdMs == vMax);
                QVERIFY(m.visAosJdMs <= m.tcaJdMs && m.tcaJdMs <= m.visLosJdMs);
            }
            // 采样跨段单调且覆盖两段可见区间（不重复、不倒退）
            if (m.samples.size() >= 2) {
                for (size_t i = 1; i < m.samples.size(); ++i)
                    QVERIFY(m.samples[i].jdMs > m.samples[i - 1].jdMs);
            }
        }
    }

    // 最短时长过滤: 7200 s > 最长合并组（≈ 5887 s）→ 空
    OrbitCalculator::Config cfgDur;
    cfgDur.minDurationSec = 7200.0;
    OrbitCalculator calcC(cfgDur);
    QVERIFY(calcC.predict(keplerFn(90.0), obs, JD0, 24.0).empty());
}

void TstOrbitCalc::samplesCoverPass()
{
    Observer obs(60.0, 0.0, 0.0, 10.0);
    OrbitCalculator calc;
    auto passes = calc.predict(keplerFn(90.0), obs, JD0, 24.0);
    QVERIFY(passes.size() >= 4);

    const auto &ev = passes[0];
    QVERIFY2(!ev.samples.empty(), "过境弧段未采样");
    QVERIFY(ev.samples.size() >= 10);

    // 采样时间单调递增且位于 [AOS, LOS] 内
    int64_t prev = ev.samples.front().jdMs;
    QVERIFY(prev >= ev.aosJdMs);
    for (size_t k = 1; k < ev.samples.size(); ++k) {
        QVERIFY(ev.samples[k].jdMs > prev);
        prev = ev.samples[k].jdMs;
    }
    QVERIFY(prev <= ev.losJdMs);

    // 采样步长 ≈ 15 s（截断误差 ≤ 1 ms）
    const double stepSec = (ev.samples[1].jdMs - ev.samples[0].jdMs) / 1000.0;
    QVERIFY2(std::abs(stepSec - 15.0) < 0.5,
             qPrintable(QString("step=%1 s").arg(stepSec, 0, 'f', 3)));

    // 采样点仰角 ≥ 掩蔽角（允许二分残余误差）
    for (const auto &s : ev.samples) {
        QVERIFY(s.look.elevDeg >= 10.0 - 0.1);
        QVERIFY(s.look.rangeKm > 0.0);
    }

    // 首/末采样时刻与 AOS/LOS 时刻一致（±1 步长）
    QVERIFY(std::abs(ev.samples.front().jdMs - ev.aosJdMs) < 16000);
    QVERIFY(std::abs(ev.samples.back().jdMs - ev.losJdMs) < 16000);

    // 关闭采样 → 空
    OrbitCalculator::Config cfg;
    cfg.enableSamples = false;
    OrbitCalculator calcNo(cfg);
    auto passes2 = calcNo.predict(keplerFn(90.0), obs, JD0, 24.0);
    QVERIFY(!passes2.empty());
    for (const auto &p : passes2) QVERIFY(p.samples.empty());
}

// 可见弧段子区间与 TCA 时刻可见性分类的一致性
void TstOrbitCalc::visibleArcConsistentWithVisibility()
{
    Observer obs(60.0, 0.0, 0.0, 10.0);
    OrbitCalculator calc;
    calc.setSatelliteName("SYNTH-LEO");
    auto passes = calc.predict(keplerFn(90.0), obs, JD0, 24.0);
    QVERIFY(!passes.empty());

    for (const auto &ev : passes) {
        // 区间有效性（有弧段时）
        if (ev.hasVisibleArc) {
            QVERIFY(ev.visAosJdMs <= ev.visLosJdMs);
            QVERIFY(ev.visAosJdMs >= ev.aosJdMs);
            QVERIFY(ev.visLosJdMs <= ev.losJdMs);
        }
        // 可见性分类 = 可见 ⇒ 弧段必存在且包含 TCA
        //（分类以 TCA 时刻判定; 弧段扫描网格强制纳入 TCA，两者判据一致）
        if (ev.visibility == PassVisibility::Visible) {
            QVERIFY2(ev.hasVisibleArc, "可见过境必须存在可见弧段");
            QVERIFY(ev.visAosJdMs <= ev.tcaJdMs);
            QVERIFY(ev.tcaJdMs <= ev.visLosJdMs);
        }
    }
}

// H3 正路径: 粗搜步长大于过境时长时, 步内 4 子步探测必须仍检出短过境
void TstOrbitCalc::shortPassWithinStepDetected()
{
    Observer obs(60.0, 0.0, 0.0, 10.0);

    // 粗步长 900s(15min) > 过境时长 ≈ 600s → 过境整体落在一个粗搜步内
    OrbitCalculator::Config cfgCoarse;
    cfgCoarse.coarseStepSec = 900.0;
    OrbitCalculator calcCoarse(cfgCoarse);
    auto coarse = calcCoarse.predict(keplerFn(90.0), obs, JD0, 24.0);

    // 细步长 15s 为基准: 两次探测的过境数应一致（子步探测不应漏检）
    OrbitCalculator calcFine;
    auto fine = calcFine.predict(keplerFn(90.0), obs, JD0, 24.0);
    QVERIFY2(!fine.empty(), "基准细步长扫描未检出过境");
    QVERIFY2(coarse.size() == fine.size(),
             qPrintable(QString("粗步长漏检: 粗=%1 细=%2")
                        .arg(coarse.size()).arg(fine.size())));

    // 检出过境的 AOS/LOS 完整性
    for (const auto &ev : coarse) {
        QVERIFY(ev.aosJdMs >= jdToMs(JD0) - 1000);
        QVERIFY(ev.aosJdMs < ev.losJdMs);
        QVERIFY(std::abs(ev.aosLook.elevDeg - 10.0) < 0.1);
        QVERIFY(std::abs(ev.losLook.elevDeg - 10.0) < 0.1);
    }
}

// H2: 跨窗口终点且 EXTEND 60min 仍不见 LOS 的高轨过境 → truncated 标志 + 截断语义
void TstOrbitCalc::truncatedPassAtWindowEnd()
{
    // 同步高度合成轨道: 相对地面静止（周期 1436 min ≈ 恒星日）
    constexpr double A_geo = 42164.0;
    const double N_geo = std::sqrt(MU / (A_geo * A_geo * A_geo));
    auto geoFn = [N_geo](double m0Deg) {
        const double m0 = m0Deg * M_PI / 180.0;
        return [m0, N_geo](double jd) -> std::optional<EciState> {
            const double u = m0 + N_geo * (jd - JD0) * 86400.0;
            const double cu = std::cos(u), su = std::sin(u);
            EciState st;
            st.posKm  = { A_geo * cu, A_geo * su, 0.0 };      // 赤道面
            st.velKms = { -A_geo * N_geo * su, A_geo * N_geo * cu, 0.0 };
            return st;
        };
    };

    Observer obs(60.0, 0.0, 0.0, 10.0);
    OrbitCalculator calc;
    bool sawTruncated = false;
    // 4 个初始相位覆盖 GEO 子点位于观测站子午线±90° 的情形——
    // 与站子午线重合的相位产生贯穿全窗口的恒定可见过境
    for (double m0 : { 0.0, 90.0, 180.0, 270.0 }) {
        auto passes = calc.predict(geoFn(m0), obs, JD0, 12.0);
        for (const auto &ev : passes) {
            if (ev.truncated) {
                sawTruncated = true;
                // 截断语义: LOS 字段/时长为窗口终点(EXTEND 终点)而非真实出境
                const double extendEnd = JD0 + (12.0 + 60.0 / 60.0) / 24.0;
                QVERIFY2(std::abs(ev.losJdMs - jdToMs(extendEnd)) < 2000,
                         qPrintable(QString("截断 LOS 应为 EXTEND 终点: %1 vs %2")
                                    .arg(ev.losJdMs).arg(jdToMs(extendEnd))));
                const double dur = static_cast<double>(ev.losJdMs - ev.aosJdMs) / 1000.0;
                QVERIFY(std::abs(dur - ev.durationSec) < 2.0);
            }
        }
    }
    QVERIFY2(sawTruncated,
             "同步轨道过境未触发 EXTEND 截断（4 个相位均未跨窗口终点）");
}

// 星等估算数值健全性
void TstOrbitCalc::magnitudeEstimateSanity()
{
    Observer obs(60.0, 0.0, 0.0, 10.0);
    OrbitCalculator calc;
    calc.setSatelliteName("ISS");   // 触发内置星等目录匹配
    auto passes = calc.predict(keplerFn(90.0), obs, JD0, 24.0);
    QVERIFY(!passes.empty());

    for (const auto &ev : passes) {
        if (ev.tcaSatShadow != ShadowStatus::InUmbra) {
            // 非本影: 星等必须有效、有限、数值合理
            QVERIFY2(ev.magValid, "非本影过境星等应有效");
            QVERIFY(std::isfinite(ev.magAtTca));
            QVERIFY(ev.magAtTca > -10.0 && ev.magAtTca < 20.0);
        } else {
            // 本影中公式前提不成立 → 标为无效
            QVERIFY(!ev.magValid);
        }
        // 采样点多普勒字段恒为有限值（enableDoppler=false 时为 0）
        for (const auto &s : ev.samples) {
            QVERIFY(std::isfinite(s.dopplerKHz));
        }
    }
}

int runTstOrbitCalc(int argc, char *argv[])
{
    TstOrbitCalc t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_orbitcalc.moc"
