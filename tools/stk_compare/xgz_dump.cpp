// =============================================================================
// xgz_dump.cpp — 用 XGZ 自己的引擎输出参考序列（供与 STK 11 逐点比对）
//
// 用法:
//   xgz_dump <TLE文件> <latDeg> <lonDeg> <altKm> <起始UTC> <结束UTC> <步长秒> <输出CSV>
//   例: xgz_dump "C:\...\BEIDOU 3M3.txt" 33.0 103.0 0.0 "2026-09-19 22:57:00" "2026-09-19 23:00:00" 1 out.csv
//
// 输出列（与 tools/stk_compare/stk_export.ps1 导出的 STK 数据同构）:
//   Time,TemeX,TemeY,TemeZ,EcefX,EcefY,EcefZ,Az,El,Range
//   - TEME  = SGP4 直接输出（TEME of Date），对应 STK 的 Cartesian Position / TEMEOfDate
//   - ECEF  = TEME→ECEF（GMST，UT1≈UTC），对应 STK 的 Cartesian Position / Fixed
//   - Az/El/Range = 站心 AER（与 STK 的 AER 报告同定义：方位角自北顺时针、仰角几何、斜距）
// 说明: 使用与主程序完全相同的 core/* 实现 + 内置 perturb（WGS72，与 STK 的 SGP4 默认一致）
// =============================================================================
#include "core/CoordTransform.h"
#include "core/Observer.h"
#include "core/Propagator.h"
#include "core/TleParser.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

double jdFromUtc(int y, int mo, int d, int h = 0, int mi = 0, double s = 0.0)
{
    y -= (mo <= 2) ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = static_cast<long long>(era) * 146097
                         + static_cast<long long>(doe) - 719468;
    return 2440587.5 + days + (h * 3600 + mi * 60 + s) / 86400.0;
}

// "2026-09-19 22:57:00" → JD（UTC）
bool parseUtc(const std::string &s, double &jdOut)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    double sec = 0.0;
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%lf", &y, &mo, &d, &h, &mi, &sec) != 6)
        return false;
    jdOut = jdFromUtc(y, mo, d, h, mi, sec);
    return true;
}

std::string fmtUtc(double jd)
{
    // JD → UTC "YYYY-MM-DD hh:mm:ss.SSS"（先量化到毫秒，避免 22:56:60 这类进位错位）
    long long ms = static_cast<long long>(std::llround((jd - 2440587.5) * 86400000.0));
    long long days = ms / 86400000LL;
    long long rem  = ms % 86400000LL;
    if (rem < 0) { rem += 86400000LL; --days; }

    // civil_from_days（Hinnant）
    const long long z = days + 719468LL;
    const long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    const unsigned doe = static_cast<unsigned>(z - era * 146097LL);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long y = static_cast<long long>(yoe) + era * 400LL;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned m = (mp < 10u) ? mp + 3u : mp - 9u;
    const long long yy = y + (m <= 2u ? 1 : 0);

    const int hh = static_cast<int>(rem / 3600000LL); rem %= 3600000LL;
    const int mi = static_cast<int>(rem / 60000LL);   rem %= 60000LL;
    const double ss = rem / 1000.0;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04lld-%02u-%02u %02d:%02d:%06.3f",
                  yy, m, d, hh, mi, ss);
    return std::string(buf);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 9) {
        // 控制台输出一律用 ASCII: Windows 控制台代码页非 UTF-8 时中文会乱码
        std::printf("usage: xgz_dump <tle-file> <latDeg> <lonDeg> <altKm> <startUTC> <stopUTC> <stepSec> <outCsv>\n"
                    "  e.g. xgz_dump tle.txt 33.0 103.0 0.05 \"2026-09-19 22:57:00\" \"2026-09-19 23:00:00\" 1 out.csv\n");
        return 2;
    }
    const std::string tlePath = argv[1];
    const double lat = std::atof(argv[2]);
    const double lon = std::atof(argv[3]);
    const double altKm = std::atof(argv[4]);
    double jdStart = 0, jdStop = 0;
    if (!parseUtc(argv[5], jdStart) || !parseUtc(argv[6], jdStop)) {
        std::printf("time format must be \"YYYY-MM-DD hh:mm:ss\"\n");
        return 2;
    }
    const double stepSec = std::atof(argv[7]);
    const std::string outPath = argv[8];
    // 步长/规模防护: 只校验 >0 时, 极小步长(如 1e-9 s)会瞬间写出几百 MB 且长时间不返回
    if (!(stepSec >= 1e-3)) {
        std::printf("step must be >= 0.001 s (got %.12g)\n", stepSec);
        return 2;
    }
    if (jdStop <= jdStart) { std::printf("invalid time range (stop <= start)\n"); return 2; }
    const double nStepsD = (jdStop - jdStart) * 86400.0 / stepSec;
    if (!(nStepsD <= 2.0e6)) {
        std::printf("too many samples: %.0f (limit 2,000,000); increase step or shorten window\n", nStepsD);
        return 2;
    }

    std::ifstream f(tlePath);
    if (!f) { std::printf("cannot open TLE file: %s\n", tlePath.c_str()); return 1; }
    std::string name, l1, l2;
    std::getline(f, name); std::getline(f, l1); std::getline(f, l2);
    auto strip = [](std::string s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
        return s;
    };
    l1 = strip(l1); l2 = strip(l2);

    TleParser::ParseError pe;
    // 三行重载: 保留卫星名称（两行重载会把名称退化成 "NORAD <编号>"，
    // 与 STK 侧名称对不上时不利于人工核对）
    const auto tle = TleParser::parse(name, l1, l2, &pe);
    if (!tle) { std::printf("TLE parse failed: %s\n", pe.detail.c_str()); return 1; }

    Propagator prop;
    std::string err;
    if (!prop.initialize(l1, l2, err, GravityModel::WGS72)) {
        std::printf("propagator init failed: %s\n", err.c_str());
        return 1;
    }
    prop.setSatelliteName(tle->name);

    // 纯几何观测量（不含大气折射），与 STK AER 报告默认口径一致
    const Observer obs(lat, lon, altKm, 0.0);

    std::printf("=== XGZ engine reference output ===\n");
    std::printf("  TLE name  : %s\n", tle->name.c_str());
    std::printf("  TLE epoch : year %d day %.8f (%.6f days before window start)\n",
                tle->epochYear, tle->epochDay,
                (jdStart - (jdFromUtc(tle->epochYear, 1, 1) + tle->epochDay - 1.0)));
    std::printf("  NORAD     : %d   model: %s\n", tle->noradID,
                prop.modelType() == Sgp4Type::LEO_SGP4 ? "SGP4" :
                (prop.modelType() == Sgp4Type::MEO_SDP4 ? "SDP4" : "SDP4(DS)"));
    std::printf("  station   : lat=%.6f lon=%.6f alt=%.6f km\n", lat, lon, altKm);
    std::printf("  station ECEF: %.6f %.6f %.6f km\n",
                obs.ecef()[0], obs.ecef()[1], obs.ecef()[2]);

    std::ofstream out(outPath, std::ios::binary);
    if (!out) { std::printf("cannot write output file: %s\n", outPath.c_str()); return 1; }
    out.precision(17);              // 关键: 默认 6 位有效数字会把 km 级位置截断到 0.1 km
    out << "Time,TemeX,TemeY,TemeZ,EcefX,EcefY,EcefZ,Az,El,Range\n";

    int rows = 0;
    // 采样时刻必须用"起点 + 整数索引 × 步长"计算:
    // 逐次 jd += step 会累积浮点误差（JD≈2.46e6 时 ulp≈41 µs/步），
    // 数百步后可达毫秒级时间漂移 —— 卫星速度 ~7.6 km/s 时即米级位置误差，
    // 足以污染与 STK 的逐点比对
    const long long nSteps = static_cast<long long>(
        std::floor((jdStop - jdStart) * 86400.0 / stepSec + 1e-9));
    for (long long i = 0; i <= nSteps; ++i) {
        const double jd = jdStart + static_cast<double>(i) * stepSec / 86400.0;
        const auto st = prop.propagate(jd);
        if (!st) continue;
        const auto ecef = CoordTransform::eciToEcef(st->posKm, jd);
        const auto look = obs.lookAngle(ecef);
        out << fmtUtc(jd) << ','
            << st->posKm[0] << ',' << st->posKm[1] << ',' << st->posKm[2] << ','
            << ecef[0] << ',' << ecef[1] << ',' << ecef[2] << ','
            << look.azDeg << ',' << look.elevDeg << ',' << look.rangeKm << '\n';
        ++rows;
    }
    out.close();
    std::printf("  wrote %d rows -> %s\n", rows, outPath.c_str());
    return 0;
}
