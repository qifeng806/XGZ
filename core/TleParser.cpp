// =============================================================================
// TleParser.cpp — TLE 两行根数解析实现
// =============================================================================
#include "TleParser.h"
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace {

// 整段消费解析（拒绝 "nan"/"inf"/部分数字如 "12x3" → 12 的静默截断）
// 前后空白允许（TLE 数字字段固定宽度带前导空格）
bool parseStrict(const std::string &raw, double &out)
{
    std::string s = raw;
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    const size_t e = s.find_last_not_of(" \t\r\n");
    s = s.substr(b, e - b + 1);

    auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return (res.ec == std::errc() && res.ptr == s.data() + s.size()
            && std::isfinite(out));
}

bool parseStrict(const std::string &raw, int &out)
{
    std::string s = raw;
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    const size_t e = s.find_last_not_of(" \t\r\n");
    s = s.substr(b, e - b + 1);
    if (s.empty()) return false;

    // from_chars(C++17) 整数版不支持 '+' 符号（TLE B* 指数常见 "+0"）
    if (s[0] == '+')
        s.erase(0, 1);
    if (s.empty()) return false;

    auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return (res.ec == std::errc() && res.ptr == s.data() + s.size());
}

} // namespace

// 从三行格式解析 TLE
std::optional<TleData> TleParser::parse(
    const std::string &line0,
    const std::string &line1,
    const std::string &line2,
    ParseError *errOut)
{
    auto result = parse(line1, line2, errOut);
    if (result) {
        // 行0 作为卫星名称（去除首尾空白）
        std::string name = line0;
        size_t first = name.find_first_not_of(" \t\r\n");
        size_t last  = name.find_last_not_of(" \t\r\n");
        if (first != std::string::npos)
            result->name = name.substr(first, last - first + 1);
    }
    return result;
}

// 从两行格式解析 TLE
std::optional<TleData> TleParser::parse(
    const std::string &line1,
    const std::string &line2,
    ParseError *errOut)
{
    TleData tle;
    ParseError err;

    // ---- 步骤 1: 长度校验 ----
    if (line1.length() < 69 || line2.length() < 69) {
        err.code = ParseError::BAD_LENGTH;
        err.detail = "TLE line too short; minimum 69 characters per line";
        if (errOut) *errOut = err;
        return std::nullopt;
    }

    // ---- 步骤 2: 行号校验 ----
    if (line1[0] != '1') {
        err.code = ParseError::BAD_LINE_NUMBER;
        err.detail = "Line 1 must start with '1'";
        if (errOut) *errOut = err;
        return std::nullopt;
    }
    if (line2[0] != '2') {
        err.code = ParseError::BAD_LINE_NUMBER;
        err.detail = "Line 2 must start with '2'";
        if (errOut) *errOut = err;
        return std::nullopt;
    }

    // ---- 步骤 3: NORAD 编号（行1 列3-7） ----
    {
        int nid = 0;
        if (!parseStrict(line1.substr(2, 5), nid)) {
            err.code = ParseError::BAD_NORAD;
            err.detail = "Invalid NORAD ID";
            if (errOut) *errOut = err;
            return std::nullopt;
        }
        tle.noradID = nid;
    }

    // 默认名称
    tle.name = "NORAD " + std::to_string(tle.noradID);

    // ---- 步骤 4: 历元（行1 列19-32） ----
    // 格式: YYDDD.DDDDDDDD（2位年 + 3位日 + 8位小数）
    {
        const std::string epochStr = line1.substr(18, 14);
        int epochYr2 = 0;
        double epochDayVal = 0.0;
        if (!parseStrict(epochStr.substr(0, 2), epochYr2)
            || !parseStrict(epochStr.substr(2), epochDayVal)
            || epochDayVal < 0.0 || epochDayVal >= 367.0) {
            err.code = ParseError::BAD_EPOCH;
            err.detail = "Invalid epoch format";
            if (errOut) *errOut = err;
            return std::nullopt;
        }

        // 2位年份 → 4位年份: ≤56 → 2000+y, >56 → 1900+y
        if (epochYr2 <= 56)
            tle.epochYear = 2000 + epochYr2;
        else
            tle.epochYear = 1900 + epochYr2;

        tle.epochDay = epochDayVal;
    }

    // ---- 步骤 5: B* 阻力项（行1 列54-61） ----
    try {
        std::string bstarStr = line1.substr(53, 8);
        // 格式: "-12345-3" → -0.12345e-3
        tle.bstar = parseScientific(bstarStr);
    } catch (...) {
        err.code = ParseError::BAD_BS;
        err.detail = "Invalid B* format";
        if (errOut) *errOut = err;
        return std::nullopt;
    }

    // ---- 步骤 6: 行2 各轨道根数 ----
    {
        auto fail = [&]() {
            err.code = ParseError::BAD_ELEMENTS;
            err.detail = "Invalid orbital element format";
            if (errOut) *errOut = err;
            return std::nullopt;
        };

        double v = 0.0;
        int    rv = 0;

        // 倾角（列9-16，度）
        if (!parseStrict(line2.substr(8, 8), v)) return fail();
        tle.inclinationDeg = v;

        // 升交点赤经（列18-25，度）
        if (!parseStrict(line2.substr(17, 8), v)) return fail();
        tle.raanDeg = v;

        // 偏心率（列27-33，省略前导"0."）
        if (!parseStrict("0." + line2.substr(26, 7), v)) return fail();
        tle.eccentricity = v;

        // 近地点辐角（列35-42，度）
        if (!parseStrict(line2.substr(34, 8), v)) return fail();
        tle.argPerigeeDeg = v;

        // 平近点角（列44-51，度）
        if (!parseStrict(line2.substr(43, 8), v)) return fail();
        tle.meanAnomalyDeg = v;

        // 平运动（列53-63，rev/day）
        if (!parseStrict(line2.substr(52, 11), v)) return fail();
        tle.meanMotionRevDay = v;

        // 圈号（列64-68）
        if (!parseStrict(line2.substr(63, 5), rv)) return fail();
        tle.revNumber = rv;
    }

    // ---- 步骤 7: 范围检查 ----
    if (tle.inclinationDeg < 0.0 || tle.inclinationDeg > 180.0) {
        err.code = ParseError::BAD_ELEMENTS;
        err.detail = "Inclination out of range [0, 180]";
        if (errOut) *errOut = err;
        return std::nullopt;
    }
    if (tle.eccentricity < 0.0 || tle.eccentricity >= 1.0) {
        err.code = ParseError::BAD_ELEMENTS;
        err.detail = "Eccentricity out of range [0, 1)";
        if (errOut) *errOut = err;
        return std::nullopt;
    }
    if (tle.meanMotionRevDay <= 0.0) {
        err.code = ParseError::BAD_ELEMENTS;
        err.detail = "Mean motion must be positive";
        if (errOut) *errOut = err;
        return std::nullopt;
    }

    // ---- 步骤 8: 模型分类 ----
    tle.ephemerisType = classifyModel(tle.meanMotionRevDay);

    // ---- 步骤 9: 校验和（可选） ----
    tle.checksumValid = verifyChecksum(line1) && verifyChecksum(line2);
    if (!tle.checksumValid) {
        err.code = ParseError::CHECKSUM_FAILED;
        err.detail = "Checksum mismatch (proceeding anyway)";
        // 不中断，仅记录
    }

    // ---- 步骤 10: 元素集编号 ----
    {
        int n = 0;
        if (!parseStrict(line1.substr(64, 4), n)) n = 0;   // 非致命，容错
        tle.elsetNum = n;
    }

    if (errOut) *errOut = err;
    return tle;
}

// 模型分类
// 与 Vallado SGP4/perturb 判据一致：轨道周期 ≥ 225 分钟（平运动 ≤ 6.4 rev/day）
// → 深空模型 SDP4；否则近地 SGP4
Sgp4Type TleParser::classifyModel(double meanMotionRevDay)
{
    if (meanMotionRevDay > 6.4)        // 周期 < 225 分钟 → 近地
        return Sgp4Type::LEO_SGP4;
    else if (meanMotionRevDay >= 1.0)  // 周期 225 分钟 ~ 24 小时 → 深空/中轨
        return Sgp4Type::MEO_SDP4;
    else                               // 周期 > 24 小时 → 深空/同步带
        return Sgp4Type::GEO_SDP4_DS;
}

// 科学记数法解析（如 " 12345-3" → 0.12345 × 10⁻³）
// 严格整段消费，指数超界（TLE 规范为 [-9, +9]）或非法字符 → 抛异常
double TleParser::parseScientific(const std::string &s)
{
    // 前6位是尾数（含前导空格或符号），后2位是指数
    // 格式: "Sm.mmmEE" 其中 S=符号/空格, m=尾数, E=指数符号, E=指数值
    if (s.length() < 8) return 0.0;

    std::string mantissaStr = s.substr(0, 6);
    std::string expStr = s.substr(6, 2);

    // 处理尾数中的空格
    for (char &c : mantissaStr) {
        if (c == ' ') c = '0';
    }
    // 确保开头有符号或数字
    if (mantissaStr[0] == ' ') mantissaStr[0] = '0';

    double mantissa = 0.0;
    int exponent = 0;
    if (!parseStrict(mantissaStr, mantissa) || !parseStrict(expStr, exponent))
        throw std::invalid_argument("bad B* field");
    if (exponent < -9 || exponent > 9)
        throw std::invalid_argument("B* exponent out of range");

    return mantissa * 1e-5 * std::pow(10.0, exponent);  // TLE格式: 隐含前导小数点
}

// 校验和验证（模 10）
bool TleParser::verifyChecksum(const std::string &line)
{
    if (line.length() < 69) return false;

    // 解析预期的校验和（第69列，即索引68）
    char checksumChar = line[68];
    int expected = (checksumChar >= '0' && checksumChar <= '9') ? (checksumChar - '0') : -1;
    if (expected < 0) return false;

    // 计算校验和（前68列）
    int sum = 0;
    for (size_t i = 0; i < 68; ++i) {
        char c = line[i];
        if (c >= '0' && c <= '9') {
            sum += (c - '0');
        } else if (c == '-') {
            sum += 1;
        }
        // 其他字符（字母、空格、小数点、'+'）贡献 0
    }
    return (sum % 10) == expected;
}
