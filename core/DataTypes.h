#ifndef CORE_MODELS_H
#define CORE_MODELS_H
// =============================================================================
// DataTypes.h — 卫星过境预报系统 全部数据结构定义
// 纯 C++17，零 Qt 依赖，可独立编译和单元测试
// =============================================================================
#include <cstdint>
#include <limits>
#include <string>
#include <array>
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------------
// JD epoch 偏移量: Unix epoch (1970-01-01T00:00:00Z) - JD0 (1858-11-17T00:00:00Z)
// 即 JD0 到 Unix epoch 的毫秒数: 2440587.5 * 86400000 = 210866760000000
// ---------------------------------------------------------------------------
constexpr int64_t kJdEpochOffsetMs = 210866760000000LL;

// ---------------------------------------------------------------------------
// 轨道模型类别
// ---------------------------------------------------------------------------
enum class Sgp4Type {
    LEO_SGP4    = 1,   // 近地：周期 < 225 min（平运动 > 6.4 rev/day）
    MEO_SDP4    = 2,   // 深空/中轨：周期 225 min ~ 1400 min（平运动 1.03 ~ 6.4 rev/day）
    GEO_SDP4_DS = 3    // 深空/同步带：周期 ≥ 1400 min（平运动 ≤ 1.03 rev/day），
                       // 含同步带 1436 min 及其上下漂移轨道、超同步处置轨道
};

// ---------------------------------------------------------------------------
// 重力模型选择（SGP4/SDP4 内部常数差异）
// ---------------------------------------------------------------------------
enum class GravityModel {
    WGS84     = 1,  // WGS-84（默认，精度更高）
    WGS72     = 0   // WGS-72 标准（NORAD TLE 传统默认）
};

// ---------------------------------------------------------------------------
// TLE 解析结果
// ---------------------------------------------------------------------------
struct TleData {
    std::string name;         // 卫星名称（来自 TLE 行0 或 NORAD 编号）
    int    noradID   = 0;     // NORAD 编号（列3-7 的数字部分）
    std::string noradStr;     // 编目号原始串: STK 按字符串保留列3-7（可含字母）
    char   classification = 'U';   // 密级（行1 列8）
    std::string intlDesignator;    // 国际编号（行1 列10-17）
    int    epochYear = 0;     // 历元年（4 位，如 2024）
    double epochDay  = 0.0;   // 历元日 + 小数（如 83.56167013）
    double nDotPer2  = 0.0;   // n/2 一阶导数项（rev/day²，行1 列34-43）
    double nDotDotPer6 = 0.0; // n/6 二阶导数项（rev/day³，行1 列45-52）
    double bstar     = 0.0;   // B* 大气阻力项（1/地球半径）
    int    tleEphemerisType = 0;   // 星历类型（行1 列63；0=Kozai 平根数, 2=Brouwer）
    double inclinationDeg  = 0.0;   // 倾角（度）
    double raanDeg         = 0.0;   // 升交点赤经（度）
    double eccentricity    = 0.0;   // 偏心率
    double argPerigeeDeg   = 0.0;   // 近地点辐角（度）
    double meanAnomalyDeg  = 0.0;   // 平近点角（度）
    double meanMotionRevDay = 0.0;  // 平运动（rev/day）
    int    revNumber = 0;           // 圈号（历元时刻）
    int    elsetNum  = 0;           // 元素集编号
    Sgp4Type ephemerisType = Sgp4Type::LEO_SGP4;   // SGP4/SDP4 模型分类
};

// ---------------------------------------------------------------------------
// TEME 系（准惯性系）瞬时状态
// ---------------------------------------------------------------------------
struct EciState {
    std::array<double, 3> posKm  = {0, 0, 0};   // 位置 (x, y, z) km
    std::array<double, 3> velKms = {0, 0, 0};   // 速度 (vx, vy, vz) km/s
};

// ---------------------------------------------------------------------------
// WGS84 大地坐标
// ---------------------------------------------------------------------------
struct GeoPos {
    double latDeg = 0.0;   // 纬度（度），北正南负，范围 [-90, +90]
    double lonDeg = 0.0;   // 经度（度），东正西负，范围 [-180, +180]
    double altKm  = 0.0;   // 椭球面以上高度（km）

    GeoPos() = default;
    GeoPos(double lat, double lon, double alt)
        : latDeg(lat), lonDeg(lon), altKm(alt) {}
};

// ---------------------------------------------------------------------------
// 站心观测量（仰角/方位角/斜距）
// ---------------------------------------------------------------------------
struct LookAngle {
    double elevDeg     = 0.0;   // 仰角（度），地平 0°，天顶 90°（几何仰角，
                                // 与 STK AER 报告同定义：不含大气折射）
    double azDeg       = 0.0;   // 方位角（度），北 0°，顺时针（东 90°，南 180°，西 270°）
    double rangeKm     = 0.0;   // 斜距（km）

    LookAngle() = default;
    LookAngle(double el, double az, double rng)
        : elevDeg(el), azDeg(az), rangeKm(rng) {}
};

// ---------------------------------------------------------------------------
// 太阳位置（地面站所见）
// ---------------------------------------------------------------------------
struct SunPosition {
    double azDeg      = 0.0;   // 太阳方位角（度），北 0°，顺时针
    double altDeg     = 0.0;   // 太阳高度角（度），地平 0°，地平下为负
    double raDeg      = 0.0;   // 太阳赤经（度）
    double decDeg     = 0.0;   // 太阳赤纬（度）
    double distanceAU = 1.0;   // 日地距离（天文单位）

    // 太阳地心方向单位矢量（赤道系，随当日历元）
    // 用于卫星地影判定前，必须先经 CoordTransform::eciDirToEcef
    // 旋转到 ECEF 地固系与卫星位置同系（EarthShadow::checkShadow 要求）
    std::array<double, 3> dirECI = {1, 0, 0};
};

// ---------------------------------------------------------------------------
// 卫星地影状态
// ---------------------------------------------------------------------------
enum class ShadowStatus {
    FullSunlight = 0,   // 完全日照
    InPenumbra   = 1,   // 半影（部分日照）
    InUmbra      = 2    // 本影（完全无光照）
};

// ---------------------------------------------------------------------------
// 过境可见性
// ---------------------------------------------------------------------------
enum class PassVisibility {
    Visible   = 0,   // 肉眼可见（暗天 + 卫星被照亮）
    RadioOnly = 1,   // 仅无线电可用（暗天 + 卫星在地影中）
    Daylight  = 2    // 白天过境（天未暗）
};

// ---------------------------------------------------------------------------
// 过境质量
// ---------------------------------------------------------------------------
enum class PassQuality {
    Excellent = 0,   // 最大仰角 ≥ 50°
    Good      = 1,   // 最大仰角 20°~50°
    Poor      = 2    // 最大仰角 < 20°
};

// ---------------------------------------------------------------------------
// JD ↔ intMs 转换（详情视图与导出共用）
// ---------------------------------------------------------------------------
inline int64_t jdToMs(double jd) {
    if (!std::isfinite(jd)) return 0;
    const double maxSafeJd = 9.2e18 / 86400000.0;
    const double minSafeJd = -9.2e18 / 86400000.0;
    if (jd > maxSafeJd) return std::numeric_limits<int64_t>::max();
    if (jd < minSafeJd) return std::numeric_limits<int64_t>::min();
    int64_t jdInt = static_cast<int64_t>(std::floor(jd));
    double jdFrac = jd - jdInt;
    int64_t intMs = jdInt * 86400000LL;
    int64_t fracMs = static_cast<int64_t>(std::round(jdFrac * 86400000.0));
    if (fracMs >= 86400000LL) { intMs += 86400000LL; fracMs -= 86400000LL; }
    else if (fracMs < 0)      { intMs -= 86400000LL; fracMs += 86400000LL; }
    return intMs + fracMs;
}
inline double msToJd(int64_t ms) {
    const int64_t msPerDay = 86400000LL;
    int64_t days = ms / msPerDay;
    int64_t rem  = ms % msPerDay;
    if (ms < 0 && rem != 0) { days--; rem += msPerDay; }
    return static_cast<double>(days) + static_cast<double>(rem) / 86400000.0;
}

// ---------------------------------------------------------------------------
// 过境弧段采样点（绘图/导出用，由 OrbitCalculator 在过境区间内等步长采集）
// ---------------------------------------------------------------------------
struct PassSample {
    int64_t  jdMs = 0;    // 时刻（毫秒儒略日）
    LookAngle look;       // 站心观测角（仰角/方位角/斜距）
    GeoPos    geo;        // 星下点（经纬高）
};

// ---------------------------------------------------------------------------
// 一次完整过境事件（AOS/TCA/LOS 三时刻的观测与光照 + 可见弧段 + 弧段采样）
// 时刻统一用 int64_t 毫秒儒略日，参考点为 JD 原点（JD×86400000 取整），
// 与 Unix epoch 毫秒（1970-01-01）不同，转换公式：
//   epoch_ms = jdMs − 210866760000000LL   （即减去 2440587.5 × 86400000）
// ---------------------------------------------------------------------------
struct PassEvent {
    std::string satelliteName;       // 卫星名称

    // ── AOS（入境/升起）时刻 ──
    int64_t     aosJdMs      = 0;
    LookAngle   aosLook;
    GeoPos      aosSatGeo;
    double      aosSunAlt    = 0.0;      // AOS 时地面站太阳高度角（度）
    ShadowStatus aosSatShadow = ShadowStatus::FullSunlight;

    // ── TCA（最大仰角）时刻 ──
    int64_t     tcaJdMs      = 0;
    double      maxElevDeg   = 0.0;      // 最大仰角（度）
    LookAngle   tcaLook;
    GeoPos      tcaSatGeo;
    double      tcaSunAlt    = 0.0;
    ShadowStatus tcaSatShadow = ShadowStatus::FullSunlight;

    // ── LOS（出境/落下）时刻 ──
    int64_t     losJdMs      = 0;
    LookAngle   losLook;
    GeoPos      losSatGeo;
    double      losSunAlt    = 0.0;
    ShadowStatus losSatShadow = ShadowStatus::FullSunlight;

    // ── 统计信息 ──
    double       durationSec     = 0.0;  // 持续时长（秒）
    PassQuality  quality         = PassQuality::Poor;
    PassVisibility visibility   = PassVisibility::Daylight;

    // ── 太阳光照比例（本影=0，全日照=1；半影带内为太阳圆面未被地球遮挡的
    //     面积比，0<f<1——与 STK Lighting 的三态口径一致，
    //     详见 EarthShadow::umbraFactor）──
    double aosIllum = 1.0;   // AOS 时刻卫星被太阳照亮的圆面比例
    double tcaIllum = 1.0;
    double losIllum = 1.0;

    // ── 可见弧段子区间（Heavens-Above 语义: 几何过境内同时满足
    //     "地面暗天 + 卫星被照亮" 的时间范围, 观测者实际可见的时间窗口）;
    //     单段过境为最大连续子区间; 合并过境（mergeGapSec）时取各段并集,
    //     可能跨过两段之间低于掩星角的间隙, 属合并语义而非真实连续可见
    int64_t visAosJdMs   = 0;   // 可见弧段起点（毫秒 JD, hasVisibleArc=false 时无效）
    int64_t visLosJdMs   = 0;   // 可见弧段终点
    bool    hasVisibleArc = false;

    // 过境延伸至预测窗口外（窗口终点截断）: LOS 各字段为截断时刻瞬时值,
    // 并非真实出境值; durationSec 为截断后时长（UI 需注明）
    bool truncated = false;

    // ── TLE 持久存储（用于按需重新传播详情视图）──
    std::string tleLine1;   // TLE 第一行（~69 字符）
    std::string tleLine2;   // TLE 第二行（~69 字符）
    // TLE 历元（解析该 TLE 时一并带出, 供 UI 显示"历元/龄期"而无需为了
    // 一次展示重新走完整解析）; tleEpochYear == 0 表示未知,
    // 调用方须回退到重新解析
    int    tleEpochYear = 0;    // 历元年（4 位）
    double tleEpochDay  = 0.0;  // 历元日 + 小数

    // 过境弧段采样（AOS→LOS 等步长采样，供极坐标/地面轨迹绘图与 KML 导出）
    std::vector<PassSample> samples;
};

#endif // CORE_MODELS_H
