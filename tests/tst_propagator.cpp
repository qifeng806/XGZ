// =============================================================================
// tst_propagator.cpp — SGP4/SDP4 传播单元测试
//
// 使用 Vallado 官方验证数据集 SGP4-VER.TLE（复制到 tests/data/）:
//   每个用例: 标准 TLE 两行 + 行尾扩展运行参数 <startmfe> <stopmfe> <deltamin>
// 验证策略（无 tcppver.out 黄金值时采用不变量校验）:
//   1) 全部用例初始化成功（短行格式用例按设计被显式拒绝）
//   2) 运行窗口内逐点传播: 正常用例全部成功，已知衰减/错误用例必须出现失败
//   3) 每个状态满足开普勒不变量（半径在近/远地点范围内、vis-viva 能量一致）
// =============================================================================
#include <QtTest/QtTest>
#include <cmath>
#include <fstream>
#include <sstream>
#include <set>
#include <vector>
#include "core/Propagator.h"
#include "core/TleParser.h"
#include "testutils.h"
#include "tst_runners.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

struct VerifCase {
    std::string line1;
    std::string line2;
    double startmfe = 0.0;
    double stopmfe  = 0.0;
    double deltamin = 0.0;
};

std::vector<VerifCase> loadVerifCases(const QString &path)
{
    std::vector<VerifCase> cases;
    std::ifstream in(path.toStdString());
    if (!in.is_open()) {
        qWarning("无法打开 %s", qPrintable(path));
        return cases;
    }

    std::string l1;
    while (std::getline(in, l1)) {
        if (l1.empty() || l1[0] == '#') continue;
        std::string l2;
        if (!std::getline(in, l2)) break;

        VerifCase c;
        c.line1 = l1;
        c.line2 = l2;

        // 行尾扩展运行参数: 最后 3 个 token 为 <startmfe> <stopmfe> <deltamin>
        // （不能用固定下标: 当圈号与前导字段粘连时行内 token 数可能为 11 或 12）
        std::istringstream ss(l2);
        std::vector<std::string> tok;
        std::string w;
        while (ss >> w) tok.push_back(w);
        if (tok.size() >= 10) {
            try {
                c.startmfe = std::stod(tok[tok.size() - 3]);
                c.stopmfe  = std::stod(tok[tok.size() - 2]);
                c.deltamin = std::stod(tok[tok.size() - 1]);
            } catch (const std::exception &) {
                // 畸形 token 不应让整个测试进程崩溃——跳过该用例并留待
                // 运行参数缺失时由 runWindow 以默认窗口执行
                c.startmfe = 0.0;
                c.stopmfe  = 1440.0;
                c.deltamin = 720.0;
            }
        }
        cases.push_back(std::move(c));
    }
    return cases;
}

// 在运行窗口内采样传播，返回是否全部成功
bool runWindow(Propagator &p, const TleData &tle,
               double startmfe, double stopmfe, double deltamin)
{
    double t = startmfe;
    if (std::abs(t) > 1e-8) t -= deltamin;
    // 开普勒不变量（由 TLE 均根数导出）
    const double mu   = 398600.8;
    const double a    = std::cbrt(mu / std::pow(tle.meanMotionRevDay * 2.0 * M_PI / 86400.0, 2.0));
    const double e    = tle.eccentricity;
    const double rPer = a * (1.0 - e);
    const double rApo = a * (1.0 + e);
    int count = 0;
    double lastR = 0.0;
    while ((t < stopmfe || count == 0) && count < 5000) {
        t = std::min(t + deltamin, stopmfe);
        if (deltamin <= 0.0) break;
        auto st = p.propagate(p.epochJD() + t / 1440.0);
        if (!st.has_value()) {
            // 大气阻尼/共振激发使卫星再入时 SGP4 中途报错（官方数据集含此类用例）。
            // 若上一采样点地心距已低于原近地点半径, 视为合理再入。
            if (lastR > 0.0 && lastR < rPer) {
                qWarning("NORAD %d 于 t=%.1f min 再入", p.noradID(), t);
                return true;
            }
            qWarning("NORAD %d t=%.1f min 传播失败", p.noradID(), t);
            return false;
        }
        // 开普勒不变量
        const double r = std::sqrt(st->posKm[0] * st->posKm[0]
                                 + st->posKm[1] * st->posKm[1]
                                 + st->posKm[2] * st->posKm[2]);
        // 短期窗口内振动量级小, 地心距应贴近均根数近/远地点 ±10%;
        // 长窗口(数年)受日月共振等影响, 偏心拱点漂移远超该范围,
        // 此时仅以 aOsc 不变性 + 物理上下界校验。
        const bool shortWindow = stopmfe <= 4320.0;   // 窗口终点距历元 ≤ 3 天
        if (shortWindow && (r < rPer * 0.90 || r > rApo * 1.10)) {
            qWarning("NORAD %d t=%.1fmin r=%.1f 允许 [%.1f, %.1f]",
                     p.noradID(), t, r, rPer * 0.90, rApo * 1.10);
            return false;
        }
        const double v2 = st->velKms[0] * st->velKms[0]
                        + st->velKms[1] * st->velKms[1]
                        + st->velKms[2] * st->velKms[2];
        const double aOsc = 1.0 / (2.0 / r - v2 / mu);
        if (std::abs(aOsc - a) / a > 0.05) {
            qWarning("NORAD %d t=%.1fmin aOsc=%.1f aTle=%.1f", p.noradID(), t, aOsc, a);
            return false;
        }
        if (r < 1e2 || r > 5e5) return false;
        const double v = std::sqrt(v2);
        if (v < 1e-3 || v > 15.0) return false;
        lastR = r;
        ++count;
    }
    return count > 0;
}

// 在 [t0, t1] 内每 step 分钟采样，直到出现传播失败
bool firstFailureWithin(Propagator &p, double t0, double t1, double step)
{
    for (double t = t0; t <= t1 + 1e-9; t += step) {
        if (!p.propagate(p.epochJD() + t / 1440.0).has_value()) return true;
    }
    return false;
}

// 期望的轨道模型分类（由周期独立推导，与 Propagator/TleParser 同带:
// n>6.4→LEO, 1440/1400≈1.0286<n≤6.4→MEO, n≤1.0286→GEO 同步带/超同步）
Sgp4Type expectModel(const std::string &norad)
{
    static const std::vector<std::string> leo = {
        "00005", "06251", "22312", "28057", "28350", "28872", "29141", "29238", "88888",
    };
    static const std::vector<std::string> meo = {
        "04632", "08195", "09880", "09998", "11801", "16925", "21897", "22674",
        "23177", "23599", "26975", "28129", "28623",
    };
    for (const auto &s : leo) if (norad == s) return Sgp4Type::LEO_SGP4;
    for (const auto &s : meo) if (norad == s) return Sgp4Type::MEO_SDP4;
    // 其余 → GEO（含近同步漂移 24208/25954/26900/28626, 14128, 20413, 23333, 33334）
    return Sgp4Type::GEO_SDP4_DS;
}

// 已知在窗口内衰减/出错的用例
bool isFailingCase(const std::string &norad)
{
    static const char *f[] = { "22312", "28872", "29141", "33333", "33334", "33335" };
    for (const char *s : f) if (norad == s) return true;
    return false;
}

} // namespace

class TstPropagator : public QObject {
    Q_OBJECT
private slots:
    void init();
    void verifyDatasetAllCases();
    void failingCasesDecay();
    void issBasicPropagation();
    void decayCaseSubOrbital();
    void keplerPeriodicity();
    void epochConsistency();
    void shortLineRejected();
    void moveSemantics();

private:
    QString m_verPath;
};

void TstPropagator::init()
{
    m_verPath = findTestData("SGP4-VER.TLE");
    QVERIFY2(!m_verPath.isEmpty(),
             "缺少测试数据 tests/data/SGP4-VER.TLE（从 perturb-master/tests 复制）");
}

void TstPropagator::verifyDatasetAllCases()
{
    const auto cases = loadVerifCases(m_verPath);
    QVERIFY2(cases.size() >= 30, "SGP4-VER.TLE 用例数不足");

    int checked = 0;
    int rejected = 0;
    for (const auto &c : cases) {
        std::string err;
        Propagator p;
        const bool ok = p.initialize(c.line1, c.line2, err);

        const std::string norad = c.line1.substr(2, 5);

        // 防御分支: 短行（<69 字符）用例按标准 69 字符格式显式拒绝。
        // 当前数据集 33 对 TLE 均 ≥69 字符, 该分支实际不触发, 仅作格式盾
        if (c.line1.size() < 69 || c.line2.size() < 69) {
            QVERIFY2(!ok, qPrintable(QString("NORAD %1 短行应被拒绝").arg(norad.c_str())));
            ++rejected;
            continue;
        }

        if (isFailingCase(norad)) continue;   // failingCasesDecay 处理

        QVERIFY2(ok, qPrintable(QString("初始化失败 NORAD %1: %2")
                                .arg(norad.c_str()).arg(QString::fromStdString(err))));
        // 数据集含 1980 年起的旧用例（11801 等），范围取 1968~2025
        QVERIFY2(p.epochJD() > 2440000.0 && p.epochJD() < 2465000.0,
                 qPrintable(QString("NORAD %1 历元 JD 异常: %2")
                            .arg(norad.c_str()).arg(p.epochJD(), 0, 'f', 4)));
        const Sgp4Type got = p.modelType();
        const Sgp4Type exp = expectModel(norad);
        QVERIFY2(got == exp,
                 qPrintable(QString("NORAD %1 分类不符: 实际=%2 期望=%3")
                            .arg(norad.c_str()).arg(static_cast<int>(got))
                            .arg(static_cast<int>(exp))));

        auto tle = TleParser::parse(c.line1, c.line2);
        QVERIFY(tle.has_value());

        QVERIFY2(runWindow(p, *tle, c.startmfe, c.stopmfe, c.deltamin),
                 qPrintable(QString("NORAD %1 窗口 [%2, %3] min 传播失败")
                            .arg(norad.c_str()).arg(c.startmfe, 0, 'f', 1)
                            .arg(c.stopmfe, 0, 'f', 1)));
        ++checked;
    }
    QVERIFY(checked >= 20);
}

void TstPropagator::failingCasesDecay()
{
    const auto cases = loadVerifCases(m_verPath);
    std::set<std::string> seen;   // 数据集含重复用例（同 NORAD 多个窗口）
    int handled = 0;

    for (const auto &c : cases) {
        const std::string norad = c.line1.substr(2, 5);
        if (!isFailingCase(norad)) continue;
        if (!seen.insert(norad).second) continue;

        std::string err;
        Propagator p;
        const bool ok = p.initialize(c.line1, c.line2, err);

        // 注入错误用例行为各异（见 SGP4-VER.TLE 数据集注释）:
        //   33334: n≈0 的极端平根数 → 初始化必须失败
        //   33335: e≈0 的 GEO，近地点永不为负 → 全窗口正常传播（不衰减）
        //   33333: e=0.995 → 近地点 < 98 km → 必衰减（走下方通用分支）
        if (norad == "33334") {
            QVERIFY2(!ok, "33334 极端平根数应初始化失败");
            ++handled; continue;
        }
        if (norad == "33335") {
            QVERIFY2(ok, qPrintable(QString("NORAD %1 初始化失败: %2")
                                    .arg(norad.c_str()).arg(QString::fromStdString(err))));
            QVERIFY2(!firstFailureWithin(p, c.startmfe, c.stopmfe, 20.0),
                     "33335 (e≈0 GEO) 不应在窗口内衰减");
            ++handled; continue;
        }

        QVERIFY2(ok, qPrintable(QString("NORAD %1 初始化失败: %2")
                                .arg(norad.c_str()).arg(QString::fromStdString(err))));

        // 窗口内必须出现传播失败（衰减）
        QVERIFY2(firstFailureWithin(p, c.startmfe, c.stopmfe, 10.0),
                 qPrintable(QString("NORAD %1 在窗口 [%2, %3] 内未衰减")
                            .arg(norad.c_str()).arg(c.startmfe, 0, 'f', 1)
                            .arg(c.stopmfe, 0, 'f', 1)));
        ++handled;
    }
    // 数据文件若新增注入用例, 计数只增不减（与 isFailingCase 表同源维护）
    QVERIFY2(handled >= 6, qPrintable(QString("注入错误用例处理数异常: %1").arg(handled)));
}

void TstPropagator::issBasicPropagation()
{
    const char *L1 = "1 25544U 98067A   22071.78032407  .00021395  00000-0  39008-3 0  9996";
    const char *L2 = "2 25544  51.6424  94.0370 0004047 256.5103  89.8846 15.49386383330227";

    std::string err;
    Propagator p;
    QVERIFY(p.initialize(L1, L2, err));
    QCOMPARE(p.modelType(), Sgp4Type::LEO_SGP4);
    QCOMPARE(p.noradID(), 25544);
    QVERIFY(std::abs(p.epochJD() - jdFromUtc(2022, 3, 12, 18, 43, 40)) < 1e-6);

    auto st = p.propagate(p.epochJD());
    QVERIFY(st.has_value());
    const double r = std::sqrt(st->posKm[0] * st->posKm[0]
                             + st->posKm[1] * st->posKm[1]
                             + st->posKm[2] * st->posKm[2]);
    const double v = std::sqrt(st->velKms[0] * st->velKms[0]
                             + st->velKms[1] * st->velKms[1]
                             + st->velKms[2] * st->velKms[2]);
    QVERIFY(r > 6600.0 && r < 6800.0);   // ~410 km 圆轨道
    QVERIFY(v > 7.4 && v < 7.9);          // ~7.66 km/s

    // 一周内高度与速度保持稳定（±5%）
    for (double mins = 0; mins <= 7 * 24 * 60; mins += 60) {
        auto s = p.propagate(p.epochJD() + mins / 1440.0);
        QVERIFY(s.has_value());
        const double rr = std::sqrt(s->posKm[0] * s->posKm[0]
                                  + s->posKm[1] * s->posKm[1]
                                  + s->posKm[2] * s->posKm[2]);
        QVERIFY(std::abs(rr - r) / r < 0.05);
    }
}

void TstPropagator::decayCaseSubOrbital()
{
    // 28872: 亚轨道（近地点 −51 km），约 50 分钟内衰减丢失
    const char *L1 = "1 28872U 05037B   05333.02012661  .25992681  00000-0  24476-3 0  1534";
    const char *L2 = "2 28872  96.4736 157.9986 0303955 244.0492 110.6523 16.46015938 10708";

    std::string err;
    Propagator p;
    QVERIFY(p.initialize(L1, L2, err));
    QCOMPARE(p.modelType(), Sgp4Type::LEO_SGP4);

    // 历元处仍可传播
    QVERIFY(p.propagate(p.epochJD()).has_value());

    // 60 分钟窗口内必然衰减
    QVERIFY2(firstFailureWithin(p, 5.0, 60.0, 5.0),
             "28872 亚轨道卫星 60 分钟内未衰减，传播行为异常");
}

void TstPropagator::keplerPeriodicity()
{
    // 00005: e=0.186 非近圆轨道, 133 分钟周期, 整周期后位置应回位
    //（周期一致性校验: 传播一个整周期与历元状态对齐）
    const char *L1 = "1 00005U 58002B   00179.78495062  .00000023  00000-0  28098-4 0  4753";
    const char *L2 = "2 00005  34.2682 348.7242 1859667 331.7664  19.3264 10.82419157413667";

    std::string err;
    Propagator p;
    QVERIFY(p.initialize(L1, L2, err));

    const double T = 1440.0 / 10.82419157413667;   // ≈ 133.03 min
    auto st0 = p.propagate(p.epochJD());
    QVERIFY(st0.has_value());

    for (int k = 1; k <= 6; ++k) {
        auto stT = p.propagate(p.epochJD() + k * T / 1440.0);
        QVERIFY(stT.has_value());
        for (int i = 0; i < 3; ++i) {
            QVERIFY(std::abs(stT->posKm[i] - st0->posKm[i]) < 200.0);
        }
    }
}

void TstPropagator::epochConsistency()
{
    const char *L1 = "1 25544U 98067A   22071.78032407  .00021395  00000-0  39008-3 0  9996";
    const char *L2 = "2 25544  51.6424  94.0370 0004047 256.5103  89.8846 15.49386383330227";

    auto tle = TleParser::parse(L1, L2);
    QVERIFY(tle.has_value());

    std::string err;
    Propagator p;
    QVERIFY(p.initialize(L1, L2, err));

    const double mu = 398600.8;
    const double aTle = std::cbrt(mu
        / std::pow(tle->meanMotionRevDay * 2.0 * M_PI / 86400.0, 2.0));
    QVERIFY(std::abs(aTle - 6793.0) < 10.0);   // ISS 半长轴 ≈ 6793 km

    auto st = p.propagate(p.epochJD());
    QVERIFY(st.has_value());
    const double r = std::sqrt(st->posKm[0] * st->posKm[0]
                             + st->posKm[1] * st->posKm[1]
                             + st->posKm[2] * st->posKm[2]);
    const double v2 = st->velKms[0] * st->velKms[0]
                    + st->velKms[1] * st->velKms[1]
                    + st->velKms[2] * st->velKms[2];
    const double aOsc = 1.0 / (2.0 / r - v2 / mu);
    QVERIFY(std::abs(aOsc - aTle) < 50.0);      // 半长轴差 < 50 km

    // 轨道倾角与 TLE 一致（±2°）
    const double hz = st->posKm[0] * st->velKms[1] - st->posKm[1] * st->velKms[0];
    const double hx = st->posKm[1] * st->velKms[2] - st->posKm[2] * st->velKms[1];
    const double hy = st->posKm[2] * st->velKms[0] - st->posKm[0] * st->velKms[2];
    const double h  = std::sqrt(hx * hx + hy * hy + hz * hz);
    const double incl = std::acos(hz / h) * 180.0 / M_PI;
    QVERIFY(std::abs(incl - 51.6424) < 2.0);
}

void TstPropagator::shortLineRejected()
{
    const char *L1 = "1 25544U 98067A   22071.78032407  .00021395  00000-0  39008-3 0  9996";
    const char *L2 = "2 25544  51.6424  94.0370 0004047 256.5103  89.8846 15.49386383330227";

    std::string err;
    Propagator p;

    // 截断到 40 字符（< 69）→ 显式拒绝并填充错误信息（与 TleParser::BAD_LENGTH 对齐）
    std::string short1 = L1;
    short1.resize(40);
    QVERIFY(!p.initialize(short1, L2, err));
    QVERIFY(!err.empty());
    QVERIFY(!p.isInitialized());

    std::string short2 = L2;
    short2.resize(40);
    QVERIFY(!p.initialize(L1, short2, err));
    QVERIFY(!err.empty());

    // 空行
    QVERIFY(!p.initialize("", "", err));
}

void TstPropagator::moveSemantics()
{
    const char *L1 = "1 25544U 98067A   22071.78032407  .00021395  00000-0  39008-3 0  9996";
    const char *L2 = "2 25544  51.6424  94.0370 0004047 256.5103  89.8846 15.49386383330227";

    std::string err;
    Propagator p;
    QVERIFY(p.initialize(L1, L2, err));

    Propagator moved = std::move(p);
    QVERIFY(!p.isInitialized());          // 源对象失效
    QVERIFY(moved.isInitialized());
    QCOMPARE(moved.noradID(), 25544);
    QCOMPARE(moved.modelType(), Sgp4Type::LEO_SGP4);

    auto st = moved.propagate(moved.epochJD());
    QVERIFY(st.has_value());              // 目标可正常传播

    // 移动赋值: 目标覆盖、源失效
    std::string err2;
    Propagator other;
    QVERIFY(other.initialize(L1, L2, err2));
    other = std::move(moved);
    QVERIFY(!moved.isInitialized());
    QVERIFY(other.isInitialized());
    QVERIFY(other.propagate(other.epochJD()).has_value());
}

int runTstPropagator(int argc, char *argv[])
{
    TstPropagator t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_propagator.moc"
