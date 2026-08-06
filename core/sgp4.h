#ifndef CORE_SGP4_H
#define CORE_SGP4_H
// =============================================================================
// sgp4.h — SGP4/SDP4 轨道传播器（基于 Vallado 公开域算法）
// 支持近地（SGP4）和深空（SDP4）自动切换
// 纯 C++17，零外部依赖
// =============================================================================
#include <cstdint>
#include <array>

// ---------------------------------------------------------------------------
// 引力常数类型
// ---------------------------------------------------------------------------
enum class GravConst {
    WGS72_OLD = 0,   // 原始 WGS-72（低精度）
    WGS72     = 1,   // WGS-72（改进）
    WGS84     = 2    // WGS-84（现代标准，推荐）
};

// ---------------------------------------------------------------------------
// 卫星记录 — SGP4 内部状态机容器
// 此结构体包含所有轨道参数和内部缓存，调用方只需填充初始值后传给 sgp4()
// ---------------------------------------------------------------------------
struct ElsetRec {
    // ===== 卫星标识 =====
    int64_t satnum    = 0;     // NORAD 编号
    int     epochYr   = 0;     // 历元年（4 位）
    double  epochDays = 0.0;   // 历元日（含小数）
    double  jdsatepoch = 0.0;  // 历元儒略日
    double  jdsatepochF = 0.0; // 历元儒略日（分数部分）

    // ===== 轨道根数（弧度制） =====
    double  bstar  = 0.0;      // B* 阻力项（1/地球半径）
    double  ndot   = 0.0;      // 平运动一阶导数（rad/min²）
    double  nddot  = 0.0;      // 平运动二阶导数（rad/min³）
    double  inclo  = 0.0;      // 倾角（rad）
    double  nodeo  = 0.0;      // 升交点赤经（rad）
    double  ecco   = 0.0;      // 偏心率
    double  argpo  = 0.0;      // 近地点辐角（rad）
    double  mo     = 0.0;      // 平近点角（rad）
    double  no_kozai = 0.0;    // 平运动（Kozai，rad/min）

    // ===== 运行模式 =====
    char    method = 'n';      // 'n'=近地, 'd'=深空
    char    operationmode = 'i'; // 'i'=改进, 'a'=afspc
    char    init   = 'n';      // 初始化标志
    int     isimp  = 0;        // 初始化模式

    // ===== 内部缓存（SGP4 计算中间量） =====
    double  aycof = 0.0, con41 = 0.0, cc1 = 0.0, cc4 = 0.0, cc5 = 0.0;
    double  d2 = 0.0, d3 = 0.0, d4 = 0.0, delmo = 0.0, eta = 0.0;
    double  argpdot = 0.0, omgcof = 0.0, sinmao = 0.0;
    double  t2cof = 0.0, t3cof = 0.0, t4cof = 0.0, t5cof = 0.0;
    double  x1mth2 = 0.0, x7thm1 = 0.0;
    double  coef  = 0.0;
    double  mdot = 0.0, nodedot = 0.0, xlcof = 0.0, xmcof = 0.0, nodecf = 0.0;

    // ===== 深空（SDP4）缓存 =====
    int     irez  = 0;
    double  d2201 = 0.0, d2211 = 0.0, d3210 = 0.0, d3222 = 0.0;
    double  d4410 = 0.0, d4422 = 0.0, d5220 = 0.0, d5232 = 0.0;
    double  d5421 = 0.0, d5433 = 0.0;
    double  dedt = 0.0, del1 = 0.0, del2 = 0.0, del3 = 0.0;
    double  didt = 0.0, dmdt = 0.0, dnodt = 0.0, domdt = 0.0;
    double  e3 = 0.0, ee2 = 0.0, peo = 0.0, pgho = 0.0, pho = 0.0;
    double  pinco = 0.0, plo = 0.0;
    double  se2 = 0.0, se3 = 0.0, sgh2 = 0.0, sgh3 = 0.0, sgh4 = 0.0;
    double  sh2 = 0.0, sh3 = 0.0, si2 = 0.0, si3 = 0.0;
    double  sl2 = 0.0, sl3 = 0.0, sl4 = 0.0;
    double  gsto = 0.0;
    double  xfact = 0.0, xgh2 = 0.0, xgh3 = 0.0, xgh4 = 0.0;
    double  xh2 = 0.0, xh3 = 0.0, xi2 = 0.0, xi3 = 0.0;
    double  xl2 = 0.0, xl3 = 0.0, xl4 = 0.0;
    double  xlamo = 0.0, zmol = 0.0, zmos = 0.0;
    double  atime = 0.0, xli = 0.0, xni = 0.0;

    // ===== 引力常数缓存 =====
    double  tumin = 0.0;        // 时间单位转换（min → TU）
    double  mu    = 0.0;        // 地心引力常数（km³/s² 或 ER³/min²）
    double  radiusearthkm = 0.0; // 地球半径（km）
    double  xke   = 0.0;        // √(GM)
    double  j2    = 0.0, j3 = 0.0, j4 = 0.0, j3oj2 = 0.0;

    // ===== 误差标志 =====
    int     error = 0;          // 0 = 无错误，非零见 sgp4Error 枚举
};

// ---------------------------------------------------------------------------
// SGP4 错误码
// ---------------------------------------------------------------------------
enum Sgp4Error {
    SGP4_OK             = 0,
    SGP4_ERR_MEAN       = 1,    // 平均根数异常
    SGP4_ERR_DECAY      = 2,    // 卫星已衰减（近地点 < 地球半径）
    SGP4_ERR_NEARLY     = 3,    // 远地点异常
    SGP4_ERR_DIVERGENCE = 4,    // 迭代发散
    SGP4_ERR_INIT       = 5     // 未初始化
};

// =============================================================================
// 公共接口
// =============================================================================

// 获取引力常数集
void getGravConst(GravConst whichconst,
                  double &tumin, double &mu, double &radiusearthkm,
                  double &xke, double &j2, double &j3, double &j4, double &j3oj2);

// 初始化卫星记录（从 TLE 轨道根数填充 ElsetRec 并调用内部初始化）
// 返回 true 表示成功，false 表示参数异常
bool sgp4Init(GravConst whichconst, char opsmode,
              int satn, double epochJD, double bstar,
              double ndot, double nddot,
              double ecco, double argpo, double inclo,
              double mo, double no_kozai, double nodeo,
              ElsetRec &satrec);

// SGP4/SDP4 传播主入口
// tsince: 距历元的分钟数（可正可负，正=未来，负=过去）
// r[3]:   输出 TEME 系位置（km）
// v[3]:   输出 TEME 系速度（km/s）
// 返回 true 表示成功
bool sgp4(ElsetRec &satrec, double tsince, double r[3], double v[3]);

// 儒略日 → GMST 弧度
double gstimeSgp4(double jdut1);

// 公历日期 → 儒略日
double jdaySgp4(int year, int mon, int day, int hr, int minute, double sec);

// 儒略日 → 公历日期
void invjdaySgp4(double jd, int &year, int &mon, int &day,
                 int &hr, int &minute, double &sec);

#endif // CORE_SGP4_H
