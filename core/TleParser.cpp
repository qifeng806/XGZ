// =============================================================================
// TleParser.cpp — TLE 两行根数解析实现
// =============================================================================
#include "TleParser.h"
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <iomanip>

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
    try {
        std::string noradStr = line1.substr(2, 5);
        tle.noradID = std::stoi(noradStr);
    } catch (...) {
        err.code = ParseError::BAD_NORAD;
        err.detail = "Invalid NORAD ID";
        if (errOut) *errOut = err;
        return std::nullopt;
    }

    // 默认名称
    tle.name = "NORAD " + std::to_string(tle.noradID);

    // ---- 步骤 4: 历元（行1 列19-32） ----
    // 格式: YYDDD.DDDDDDDD（2位年 + 3位日 + 8位小数）
    try {
        std::string epochStr = line1.substr(18, 14);
        int epochYr2 = std::stoi(epochStr.substr(0, 2));
        double epochDayVal = std::stod(epochStr.substr(2));

        // 2位年份 → 4位年份: ≤56 → 2000+y, >56 → 1900+y
        if (epochYr2 <= 56)
            tle.epochYear = 2000 + epochYr2;
        else
            tle.epochYear = 1900 + epochYr2;

        tle.epochDay = epochDayVal;
    } catch (...) {
        err.code = ParseError::BAD_EPOCH;
        err.detail = "Invalid epoch format";
        if (errOut) *errOut = err;
        return std::nullopt;
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
    try {
        // 倾角（列9-16，度）
        tle.inclinationDeg = std::stod(line2.substr(8, 8));

        // 升交点赤经（列18-25，度）
        tle.raanDeg = std::stod(line2.substr(17, 8));

        // 偏心率（列27-33，省略前导"0."）
        std::string eccStr = line2.substr(26, 7);
        tle.eccentricity = std::stod("0." + eccStr);

        // 近地点辐角（列35-42，度）
        tle.argPerigeeDeg = std::stod(line2.substr(34, 8));

        // 平近点角（列44-51，度）
        tle.meanAnomalyDeg = std::stod(line2.substr(43, 8));

        // 平运动（列53-63，rev/day）
        tle.meanMotionRevDay = std::stod(line2.substr(52, 11));

        // 圈号（列64-68）
        tle.revNumber = static_cast<int>(std::stod(line2.substr(63, 5)));
    } catch (...) {
        err.code = ParseError::BAD_ELEMENTS;
        err.detail = "Invalid orbital element format";
        if (errOut) *errOut = err;
        return std::nullopt;
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
    try {
        tle.elsetNum = std::stoi(line1.substr(64, 4));
    } catch (...) {
        tle.elsetNum = 0;
    }

    if (errOut) *errOut = err;
    return tle;
}

// 模型分类
Sgp4Type TleParser::classifyModel(double meanMotionRevDay)
{
    if (meanMotionRevDay > 15.65)
        return Sgp4Type::LEO_SGP4;
    else if (meanMotionRevDay >= 1.0)
        return Sgp4Type::MEO_SDP4;
    else
        return Sgp4Type::GEO_SDP4_DS;
}

// 科学记数法解析（如 " 12345-3" → 0.12345 × 10⁻³）
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

    double mantissa = std::stod(mantissaStr) * 1e-5;  // TLE格式: 隐含前导小数点
    int exponent = std::stoi(expStr);

    return mantissa * std::pow(10.0, exponent);
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
