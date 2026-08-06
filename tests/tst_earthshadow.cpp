// =============================================================================
// tst_earthshadow.cpp — 地影判定单元测试（圆锥模型: 本影/半影/全日照）
// =============================================================================
#include <QtTest/QtTest>
#include <cmath>
#include "core/EarthShadow.h"
#include "core/CoordTransform.h"
#include "tst_runners.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
constexpr double RE = EarthShadow::EARTH_RADIUS_KM;

// 视半径（与实现同公式）
double rhoEarth(double r) { return std::asin(RE / r); }
double rhoSun(double distAU) {
    return std::asin(EarthShadow::SUN_RADIUS_KM / (distAU * EarthShadow::AU_KM));
}

// 构造夜侧卫星: 太阳方向为 +Y, 反太阳方向为 −Y
// 卫星 = r·(sinθ, −cosθ, 0)，θ = 与反太阳方向的夹角
std::array<double, 3> nightSat(double r, double thetaDeg)
{
    const double th = thetaDeg * M_PI / 180.0;
    return { r * std::sin(th), -r * std::cos(th), 0.0 };
}
} // namespace

class TstEarthShadow : public QObject {
    Q_OBJECT
private slots:
    void daySide();
    void nightSideUmbra();
    void penumbraBand();
    void coneBoundaries();
    void sunDistanceEffect();
    void farFromAxis();
    void umbraFactorLinearity();
    void frameRotationMatters();
};

void TstEarthShadow::daySide()
{
    // 卫星在日侧（与太阳方向夹角 < 90°）→ 全日照
    std::array<double, 3> sat = {0.0, 6778.137, 0.0};
    QCOMPARE(EarthShadow::checkShadow(sat, {0.0, 1.0, 0.0}),
             ShadowStatus::FullSunlight);

    // 卫星与太阳同方向（正前方远处）→ 全日照
    QCOMPARE(EarthShadow::checkShadow({20000.0, 0.0, 0.0}, {1.0, 0.0, 0.0}),
             ShadowStatus::FullSunlight);
}

void TstEarthShadow::nightSideUmbra()
{
    // 卫星在夜侧且正对地轴（theta = 0）→ 本影
    std::array<double, 3> leo = {0.0, 6778.137, 0.0};
    QCOMPARE(EarthShadow::checkShadow(leo, {0.0, -1.0, 0.0}),
             ShadowStatus::InUmbra);

    // 距轴 5000 km, theta = atan(5000/6778) = 36.4° << 本影界 → 本影
    QCOMPARE(EarthShadow::checkShadow({5000.0, 6778.137, 0.0}, {0.0, -1.0, 0.0}),
             ShadowStatus::InUmbra);

    // 任意指向的单位太阳方向：结果与方向归一化无关
    double inv = 1.0 / std::sqrt(2.0);
    std::array<double, 3> sun45 = {-inv, -inv, 0.0};
    QCOMPARE(EarthShadow::checkShadow({0.0, 6778.137, 0.0}, sun45),
             ShadowStatus::InUmbra);

    // 卫星贴着地球背面反方向 → 日侧（地心在中间）
    QCOMPARE(EarthShadow::checkShadow({0.0, -6378.137, 0.0}, {0.0, -1.0, 0.0}),
             ShadowStatus::FullSunlight);

    // GEO 卫星夜间正中 → 本影（真实 GEO 食）
    QCOMPARE(EarthShadow::checkShadow({0.0, -42164.0, 0.0}, {0.0, 1.0, 0.0}),
             ShadowStatus::InUmbra);
}

void TstEarthShadow::penumbraBand()
{
    // LEO 高度 400km: rhoE = 70.23°, rhoS = 0.27°
    // 本影界 69.96° < theta < 70.49° 之间为半影
    const double r = 6778.137;
    const double uLow  = rhoEarth(r) - rhoSun(1.0);
    const double uHigh = rhoEarth(r) + rhoSun(1.0);

    const double mid = (uLow + uHigh) / 2.0;
    QCOMPARE(EarthShadow::checkShadow(nightSat(r, mid * 180.0 / M_PI), {0.0, 1.0, 0.0}),
             ShadowStatus::InPenumbra);

    // GEO: rhoE = 8.70°: 8.44° < theta < 8.97° 半影
    const double rg = 42164.0;
    QCOMPARE(EarthShadow::checkShadow(nightSat(rg, 8.7), {0.0, 1.0, 0.0}),
             ShadowStatus::InPenumbra);

    // theta 超出半影上界 → 全日照
    QCOMPARE(EarthShadow::checkShadow(nightSat(rg, 9.1), {0.0, 1.0, 0.0}),
             ShadowStatus::FullSunlight);
}

void TstEarthShadow::coneBoundaries()
{
    // 边界精确性: 在由同一套常数算出的圆锥边界 ±1e-9 rad 处
    const double r = 8000.0;
    const double uLow  = rhoEarth(r) - rhoSun(1.0);
    const double uHigh = rhoEarth(r) + rhoSun(1.0);
    const double deg = 180.0 / M_PI;

    // 本影下界内侧 → 本影; 外侧 → 半影
    QCOMPARE(EarthShadow::checkShadow(nightSat(r, (uLow - 1e-9) * deg), {0.0, 1.0, 0.0}),
             ShadowStatus::InUmbra);
    QCOMPARE(EarthShadow::checkShadow(nightSat(r, (uLow + 1e-9) * deg), {0.0, 1.0, 0.0}),
             ShadowStatus::InPenumbra);

    // 半影上界内侧 → 半影; 外侧 → 全日照
    QCOMPARE(EarthShadow::checkShadow(nightSat(r, (uHigh - 1e-9) * deg), {0.0, 1.0, 0.0}),
             ShadowStatus::InPenumbra);
    QCOMPARE(EarthShadow::checkShadow(nightSat(r, (uHigh + 1e-9) * deg), {0.0, 1.0, 0.0}),
             ShadowStatus::FullSunlight);
}

void TstEarthShadow::sunDistanceEffect()
{
    // 太阳越近 → 视半径越大 → 本影越小
    const double r = 42164.0;         // GEO
    const double theta = 8.2;         // 8.17°(0.5AU 本影界) < 8.2° < 8.44°(1AU 本影界)

    // 1 AU: theta < 本影界 → 本影
    QCOMPARE(EarthShadow::checkShadow(nightSat(r, theta), {0.0, 1.0, 0.0}, 1.0),
             ShadowStatus::InUmbra);
    // 0.5 AU: theta > 本影界 → 退为半影
    QCOMPARE(EarthShadow::checkShadow(nightSat(r, theta), {0.0, 1.0, 0.0}, 0.5),
             ShadowStatus::InPenumbra);
    // 非法距离（0）→ 按 1 AU 处理
    QCOMPARE(EarthShadow::checkShadow(nightSat(r, theta), {0.0, 1.0, 0.0}, 0.0),
             ShadowStatus::InUmbra);
}

void TstEarthShadow::farFromAxis()
{
    // 夜侧但偏离轴很远: theta = atan(8000/6778) = 49.7°
    // rhoE = asin(6378/10485) = 37.5° < 49.7° → 全日照
    std::array<double, 3> sat = {8000.0, -6778.137, 0.0};
    QCOMPARE(EarthShadow::checkShadow(sat, {0.0, 1.0, 0.0}),
             ShadowStatus::FullSunlight);
}

void TstEarthShadow::umbraFactorLinearity()
{
    // 半影带内: 因子从本影界(0) 到半影界(1) 线性过渡
    const double r = 8000.0;
    const double uLow  = rhoEarth(r) - rhoSun(1.0);
    const double uHigh = rhoEarth(r) + rhoSun(1.0);
    const double deg = 180.0 / M_PI;
    const std::array<double, 3> SUN_DIR = {0.0, 1.0, 0.0};

    // 日侧 / 本影内部 / 全日照外侧 → 端点值
    QCOMPARE(EarthShadow::umbraFactor({0.0, 6778.137, 0.0}, SUN_DIR), 1.0);
    QCOMPARE(EarthShadow::umbraFactor(nightSat(r, 0.0), SUN_DIR), 0.0);
    QCOMPARE(EarthShadow::umbraFactor(nightSat(r, (uHigh + 1.0) * deg), SUN_DIR), 1.0);
    QCOMPARE(EarthShadow::umbraFactor(nightSat(r, 0.5 * (uLow + uHigh) * deg), SUN_DIR), 0.5);

    // 与 checkShadow 严格一致: 端点附近落在对应的三态区间
    QCOMPARE(EarthShadow::checkShadow(nightSat(r, (uLow - 1e-9) * deg), SUN_DIR),
             ShadowStatus::InUmbra);
    QCOMPARE(EarthShadow::checkShadow(nightSat(r, (uHigh + 1e-9) * deg), SUN_DIR),
             ShadowStatus::FullSunlight);
    QCOMPARE(EarthShadow::checkShadow(nightSat(r, 0.5 * (uLow + uHigh) * deg), SUN_DIR),
             ShadowStatus::InPenumbra);

    // 对称性: θ 与 θ' = (uLow+uHigh) − θ 处因子互补（半影带内）
    for (int k = 1; k <= 4; ++k) {
        double theta = uLow + (uHigh - uLow) * k / 5.0;
        double f1 = EarthShadow::umbraFactor(nightSat(r, theta * deg), SUN_DIR);
        double f2 = EarthShadow::umbraFactor(
            nightSat(r, (uLow + uHigh - theta) * deg), SUN_DIR);
        QVERIFY(std::abs((f1 + f2) - 1.0) < 1e-9);
    }
}

void TstEarthShadow::frameRotationMatters()
{
    // 回归测试（历史 bug）:
    // 太阳方向必须从 ECI 旋转到 ECEF 与卫星同系；对 GEO 卫星,
    // 用未旋转的 dirECI 与 ECEF 卫星位置点积可能得出错误日照状态
    // 选取 GMST ≈ 180° 的时刻（探针迭代求得 jd 2451805.009061）
    // 使 ECI 的 +X 方向与 ECEF 的 −X 方向几乎重合，两个坐标系差异最明显
    double jd = 2451805.009061;
    auto sunECI = CoordTransform::eciDirToEcef({1.0, 0.0, 0.0}, jd);

    // GEO 卫星在某时刻位于正对太阳的 ECEF 方向（r = 42164 km 处）
    double theta = CoordTransform::gmstRad(jd);
    std::array<double, 3> geo = {
        42164.0 * std::cos(theta),
        42164.0 * std::sin(theta),
        0.0
    };
    // ECEF 系内: geo 指向太阳 → dot > 0 → 全日照
    QCOMPARE(EarthShadow::checkShadow(geo, sunECI),
             ShadowStatus::FullSunlight);

    // 反例: 若误用未旋转的 dirECI={1,0,0}（差一个 GMST），结果可能错误
    // （此处仅验证两个坐标系下 dot 符号确实不同，说明旋转是必须的）
    double dotRotated = geo[0] * sunECI[0] + geo[1] * sunECI[1] + geo[2] * sunECI[2];
    double dotUnrotated = geo[0] * 1.0 + geo[1] * 0.0 + geo[2] * 0.0;
    QVERIFY2(dotRotated > 0.0 && dotUnrotated < 0.0,
             qPrintable(QString("旋转后 dot=%1, 未旋转 dot=%2")
                        .arg(dotRotated, 0, 'f', 3).arg(dotUnrotated, 0, 'f', 3)));
}

int runTstEarthShadow(int argc, char *argv[])
{
    TstEarthShadow t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_earthshadow.moc"
