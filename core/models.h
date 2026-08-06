#ifndef CORE_MODELS_H
#define CORE_MODELS_H
// =============================================================================
// models.h — 卫星过境预报系统 全部数据结构定义
// 纯 C++17，零 Qt 依赖，可独立编译和单元测试
// =============================================================================
#include <cstdint>
#include <string>
#include <array>
#include <vector>

// ---------------------------------------------------------------------------
// 轨道模型类别
// ---------------------------------------------------------------------------
enum class Sgp4Type {
    LEO_SGP4    = 1,   // 近地：周期 < 225 min（平运动 > 6.4 rev/day）
    MEO_SDP4    = 2,   // 深空/中轨：周期 225 min ~ 24 h（平运动 1.0 ~ 6.4 rev/day）
    GEO_SDP4_DS = 3    // 深空/同步带：周期 > 24 h（平运动 < 1.0 rev/day）
};

// ---------------------------------------------------------------------------
// TLE 解析结果
// ---------------------------------------------------------------------------
struct TleData {
    std::string name;         // 卫星名称（来自 TLE 行0 或 NORAD 编号）
    int    noradID   = 0;     // NORAD 编号
    int    epochYear = 0;     // 历元年（4 位，如 2024）
    double epochDay  = 0.0;   // 历元日 + 小数（如 83.56167013）
    double bstar     = 0.0;   // B* 大气阻力项（1/地球半径）
    double inclinationDeg  = 0.0;   // 倾角（度）
    double raanDeg         = 0.0;   // 升交点赤经（度）
    double eccentricity    = 0.0;   // 偏心率
    double argPerigeeDeg   = 0.0;   // 近地点辐角（度）
    double meanAnomalyDeg  = 0.0;   // 平近点角（度）
    double meanMotionRevDay = 0.0;  // 平运动（rev/day）
    int    revNumber = 0;           // 圈号（历元时刻）
    int    elsetNum  = 0;           // 元素集编号
    Sgp4Type ephemerisType = Sgp4Type::LEO_SGP4;
    bool   checksumValid = false;   // 校验和是否通过
    bool   isDecayed     = false;   // 是否已衰减

    // 计算轨道周期（分钟）
    double periodMinutes() const {
        if (meanMotionRevDay <= 0.0) return 0.0;
        return 1440.0 / meanMotionRevDay;
    }
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
    double elevDeg = 0.0;   // 仰角（度），地平 0°，天顶 90°
    double azDeg   = 0.0;   // 方位角（度），北 0°，顺时针（东 90°，南 180°，西 270°）
    double rangeKm = 0.0;   // 斜距（km）

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
// 过境弧段采样点（绘图/导出用，由 OrbitCalculator 在过境区间内等步长采集）
// ---------------------------------------------------------------------------
struct PassSample {
    int64_t  jdMs = 0;    // 时刻（毫秒儒略日）
    LookAngle look;       // 站心观测角（仰角/方位角/斜距）
    GeoPos    geo;        // 星下点（经纬高）
};

// ---------------------------------------------------------------------------
// 一次完整过境事件（共 22 个字段 + 弧段采样）
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
    double      aosElRefracted = 0.0;    // 大气折射修正后仰角（度）

    // ── TCA（最大仰角）时刻 ──
    int64_t     tcaJdMs      = 0;
    double      maxElevDeg   = 0.0;      // 最大仰角（度）
    LookAngle   tcaLook;
    GeoPos      tcaSatGeo;
    double      tcaSunAlt    = 0.0;
    ShadowStatus tcaSatShadow = ShadowStatus::FullSunlight;
    double      tcaElRefracted = 0.0;

    // ── LOS（出境/落下）时刻 ──
    int64_t     losJdMs      = 0;
    LookAngle   losLook;
    GeoPos      losSatGeo;
    double      losSunAlt    = 0.0;
    ShadowStatus losSatShadow = ShadowStatus::FullSunlight;
    double      losElRefracted = 0.0;

    // ── 统计信息 ──
    double       durationSec     = 0.0;  // 持续时长（秒）
    int          passType        = 0;    // 0 = 升轨，1 = 降轨
    PassQuality  quality         = PassQuality::Poor;
    PassVisibility visibility   = PassVisibility::Daylight;
    double       dopplerAtTcaKHz = 0.0;  // TCA 时刻多普勒频移（kHz）

    // 过境弧段采样（AOS→LOS 等步长采样，供极坐标/地面轨迹绘图与 KML 导出）
    std::vector<PassSample> samples;
};

#endif // CORE_MODELS_H
