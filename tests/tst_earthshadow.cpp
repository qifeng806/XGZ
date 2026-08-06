// =============================================================================
// tst_earthshadow.cpp — 地影判定单元测试（圆柱模型）
// =============================================================================
#include <QtTest/QtTest>
#include <cmath>
#include "core/EarthShadow.h"
#include "core/CoordTransform.h"
#include "tst_runners.h"

class TstEarthShadow : public QObject {
    Q_OBJECT
private slots:
    void daySide();
    void nightSideUmbra();
    void farFromAxis();
    void shadowBoundary();
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

    // 卫星在夜侧但离地轴很远 → 全日照（俯视越轴上方）
    std::array<double, 3> far = {7000.0, 6778.137, 0.0};
    QCOMPARE(EarthShadow::checkShadow(far, {0.0, -1.0, 0.0}),
             ShadowStatus::FullSunlight);
}

void TstEarthShadow::nightSideUmbra()
{
    // 卫星在夜侧且紧贴地轴（距轴 < 地球半径）→ 本影
    std::array<double, 3> leo = {0.0, 6778.137, 0.0};
    QCOMPARE(EarthShadow::checkShadow(leo, {0.0, -1.0, 0.0}),
             ShadowStatus::InUmbra);

    // 距轴 5000 km < 6371 km → 本影
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
}

void TstEarthShadow::farFromAxis()
{
    // 距轴 8000 km > 6371 km → 全日照
    std::array<double, 3> sat = {8000.0, 6778.137, 0.0};
    QCOMPARE(EarthShadow::checkShadow(sat, {0.0, -1.0, 0.0}),
             ShadowStatus::FullSunlight);
}

void TstEarthShadow::shadowBoundary()
{
    // 边界: 距轴恰为地球半径 → 临界（严格小于才判本影）
    std::array<double, 3> onEdge = {6371.0, 6371.0, 0.0};
    QCOMPARE(EarthShadow::checkShadow(onEdge, {0.0, -1.0, 0.0}),
             ShadowStatus::FullSunlight);

    // 距轴略小于半径 → 本影
    std::array<double, 3> inEdge = {6371.0 - 1e-4, 6371.0, 0.0};
    QCOMPARE(EarthShadow::checkShadow(inEdge, {0.0, -1.0, 0.0}),
             ShadowStatus::InUmbra);

    // 距轴略大于半径 → 全日照
    std::array<double, 3> outEdge = {6371.0 + 1e-4, 6371.0, 0.0};
    QCOMPARE(EarthShadow::checkShadow(outEdge, {0.0, -1.0, 0.0}),
             ShadowStatus::FullSunlight);
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
