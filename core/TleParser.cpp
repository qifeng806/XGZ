// =============================================================================
// TleParser.cpp — TLE 两行根数解析实现（口径基准: STK 11 定列读取 + 列格式校验）
// 列定义与校验规则见 TleParser.h 头部注释（逐列对应 STK 帮助 importfiles-11.htm）
// =============================================================================
#include "TleParser.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace {

// 整段消费解析（拒绝 "nan"/"inf"/部分数字如 "12x3" → 12 的静默截断）
bool parseStrict(const std::string &raw, double &out)
{
    std::string s = raw;
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    const size_t e = s.find_last_not_of(" \t\r\n");
    s = s.substr(b, e - b + 1);

    // from_chars 的浮点版不接受前导 '+'（TLE 科学记数法字段常见 "+00000-0"、
    // "+70284-4"）, 而 STK/sscanf("%lf") 接受 → 先剥掉, 保持与 STK 同口径
    if (!s.empty() && s[0] == '+')
        s.erase(0, 1);
    if (s.empty()) return false;

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

    // from_chars(C++17) 整数版不支持 '+' 符号（TLE 指数常见 "+0"）
    if (s[0] == '+')
        s.erase(0, 1);
    if (s.empty()) return false;

    auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return (res.ec == std::errc() && res.ptr == s.data() + s.size());
}

// ---- STK 列格式判据（见 STK 帮助 TLE 表格 "Format Validation" 列）----
bool isBlank(char c)      { return c == ' '; }
bool isDigit(char c)      { return c >= '0' && c <= '9'; }
bool isAlnum(char c)      { return isDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
// STK: 编目号列 3-6 "either an alphanumeric character or a blank space"
bool isAlnumOrBlank(char c) { return isAlnum(c) || isBlank(c); }
bool signOrBlank(char c)  { return c == '-' || c == '+' || c == ' '; }
bool signZeroOrBlank(char c) { return c == '-' || c == '+' || c == '0' || c == ' '; }

// 整段 [from, from+len) 满足判据
bool allOf(const std::string &s, size_t from, size_t len, bool (*pred)(char))
{
    for (size_t i = from; i < from + len; ++i)
        if (!pred(s[i])) return false;
    return true;
}
bool allDigitOrBlank(const std::string &s, size_t from, size_t len)
{
    for (size_t i = from; i < from + len; ++i)
        if (!isDigit(s[i]) && !isBlank(s[i])) return false;
    return true;
}

// 数字字段取值: 去掉空格后整段解析（STK 用 sscanf 宽域读取, 等效于忽略空格）
bool numericField(const std::string &s, size_t from, size_t len, double &out)
{
    std::string f = s.substr(from, len);
    f.erase(std::remove(f.begin(), f.end(), ' '), f.end());
    if (f.empty()) return false;
    return parseStrict(f, out);
}
bool numericField(const std::string &s, size_t from, size_t len, int &out)
{
    std::string f = s.substr(from, len);
    f.erase(std::remove(f.begin(), f.end(), ' '), f.end());
    if (f.empty()) return false;
    return parseStrict(f, out);
}

// 1 基列号 → 错误文本（便于用户定位）
std::string col(int oneBased) { return "列" + std::to_string(oneBased); }

} // namespace

// 仅剥除行尾 CR/LF: STK 定列读取不需要任何"对齐修复",
// 多余的尾部内容（如 SGP4-VER 扩展格式）也不得截断——STK 只读前 69 列
std::pair<std::string, std::string>
TleParser::trimLineEndings(const std::string &line1, const std::string &line2)
{
    auto strip = [](std::string s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
            s.pop_back();
        return s;
    };
    return { strip(line1), strip(line2) };
}

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

// 从两行格式解析 TLE（严格按 STK 列定义取数 + STK 列格式校验）
std::optional<TleData> TleParser::parse(
    const std::string &line1,
    const std::string &line2,
    ParseError *errOut)
{
    TleData tle;
    ParseError err;

    auto fail = [&](ParseError::Code code, const std::string &msg)
        -> std::optional<TleData> {
        err.code = code;
        err.detail = msg;
        if (errOut) *errOut = err;
        return std::nullopt;
    };

    const auto [l1, l2] = trimLineEndings(line1, line2);

    // ---- 行长: STK 定列读取至少需要 69 列 ----
    if (l1.length() < static_cast<size_t>(kTleLineLength)
        || l2.length() < static_cast<size_t>(kTleLineLength)) {
        return fail(ParseError::BAD_LENGTH,
                    "TLE 行长不足 69 列（STK 定列读取的最低要求）: 行1="
                    + std::to_string(l1.length()) + " 列, 行2="
                    + std::to_string(l2.length()) + " 列");
    }

    // ---- 卡号（行1 列1 / 行2 列1）与列2 空列 ----
    if (l1[0] != '1')
        return fail(ParseError::BAD_LINE_NUMBER, "行1 首列必须是 '1'");
    if (l2[0] != '2')
        return fail(ParseError::BAD_LINE_NUMBER, "行2 首列必须是 '2'");
    if (!isBlank(l1[1]))
        return fail(ParseError::BAD_FORMAT, col(2) + " 必须是空格（行1）");
    if (!isBlank(l2[1]))
        return fail(ParseError::BAD_FORMAT, col(2) + " 必须是空格（行2）");

    // ---- 编目号（行1/行2 列3-7） ----
    // STK: 3-6 列可为字母数字或空格, 第 7 列必须是字母数字; 按字符串保留
    {
        const std::string sn = l1.substr(2, 5);
        // STK 校验: 列 3-6 各自可为"字母数字或空格"; 列 7 必须是字母数字
        if (!allOf(l1, 2, 4, isAlnumOrBlank))
            return fail(ParseError::BAD_NORAD, "编目号（列3-6）必须是字母数字或空格");
        if (!isAlnum(l1[6]))
            return fail(ParseError::BAD_NORAD, "编目号第 7 列必须是字母数字");
        // 数值化: 去掉空格后若整段为数字则取数值（列3-7 允许字母, 如业余无线电
        // 编号 "A0370"）→ 此时 noradID 保持 0, 原始串仍存于 noradStr 供展示
        std::string digits = sn;
        digits.erase(std::remove(digits.begin(), digits.end(), ' '), digits.end());
        int nid = 0;
        if (!digits.empty() && parseStrict(digits, nid) && nid > 0)
            tle.noradID = nid;
        std::string raw = sn;
        while (!raw.empty() && raw.front() == ' ') raw.erase(0, 1);
        while (!raw.empty() && raw.back() == ' ')  raw.pop_back();
        tle.noradStr = raw;
        tle.name = "NORAD " + (raw.empty() ? std::string("0") : raw);
    }

    // ---- 密级（列8） / 空列9 ----
    tle.classification = l1[7];
    if (!isBlank(l1[8]))
        return fail(ParseError::BAD_FORMAT, col(9) + " 必须是空格");

    // ---- 国际编号（列10-17，STK 不校验） ----
    {
        std::string intl = l1.substr(9, 8);
        while (!intl.empty() && intl.back() == ' ') intl.pop_back();
        tle.intlDesignator = intl;
    }
    if (!isBlank(l1[17]))
        return fail(ParseError::BAD_FORMAT, col(18) + " 必须是空格");

    // ---- 历元（列19-32）: YY + DDD.DDDDDDDD ----
    {
        if (!allOf(l1, 18, 2, isDigit))
            return fail(ParseError::BAD_EPOCH, "历元年（列19-20）必须是两位数字");
        if (!allOf(l1, 20, 3, isDigit))
            return fail(ParseError::BAD_EPOCH, "历元日整数部分（列21-23）必须是数字");
        if (l1[23] != '.')
            return fail(ParseError::BAD_EPOCH, col(24) + " 必须是小数点");
        if (!allOf(l1, 24, 8, isDigit))
            return fail(ParseError::BAD_EPOCH, "历元日小数部分（列25-32）必须是 8 位数字");

        int yr2 = 0;
        if (!parseStrict(l1.substr(18, 2), yr2))
            return fail(ParseError::BAD_EPOCH, "历元年解析失败");
        // 与 SGP4/SDP4 参考实现（STK 内部使用的 CSSI 例程）同规则:
        // YY < 57 → 20YY, 否则 19YY
        tle.epochYear = (yr2 < 57) ? (2000 + yr2) : (1900 + yr2);

        double day = 0.0;
        if (!parseStrict(l1.substr(20, 12), day))
            return fail(ParseError::BAD_EPOCH, "历元日解析失败");
        // STK 校验: 3 位整数部分不得大于 366
        if (!(day > 0.0 && day < 367.0))
            return fail(ParseError::BAD_EPOCH,
                        "历元日超出 STK 允许范围: " + std::to_string(day));
        tle.epochDay = day;
    }

    // ---- n/2 一阶导数项（列34-43）: S.NNNNNNNN ----
    {
        if (!signZeroOrBlank(l1[33]))
            return fail(ParseError::BAD_FORMAT, col(34) + " 必须是 +、-、0 或空格");
        if (l1[34] != '.')
            return fail(ParseError::BAD_FORMAT, col(35) + " 必须是小数点");
        if (!allOf(l1, 35, 8, isDigit))
            return fail(ParseError::BAD_FORMAT, "n/2 小数位（列36-43）必须是数字");
        const double sign = (l1[33] == '-') ? -1.0 : 1.0;
        double frac = 0.0;
        if (!parseStrict("0." + l1.substr(35, 8), frac))
            return fail(ParseError::BAD_FORMAT, "n/2 解析失败");
        tle.nDotPer2 = sign * frac;
    }

    if (!isBlank(l1[43]))
        return fail(ParseError::BAD_FORMAT, col(44) + " 必须是空格");

    // ---- n/6 二阶导数项（列45-52）: 与 B* 同形的科学记数法 ----
    {
        if (!signOrBlank(l1[44]))
            return fail(ParseError::BAD_FORMAT, col(45) + " 必须是 +、- 或空格");
        if (!allOf(l1, 45, 5, isDigit))
            return fail(ParseError::BAD_FORMAT, "n/6 尾数（列46-50）必须是数字");
        if (!signOrBlank(l1[50]))
            return fail(ParseError::BAD_FORMAT, col(51) + " 必须是 +、- 或空格");
        if (!isDigit(l1[51]) && !isBlank(l1[51]))
            return fail(ParseError::BAD_FORMAT, col(52) + " 必须是数字或空格");
        try {
            tle.nDotDotPer6 = parseScientific(l1.substr(44, 8));
        } catch (const std::invalid_argument &e) {
            return fail(ParseError::BAD_FORMAT,
                        std::string("n/6 格式错误: ") + e.what());
        }
    }

    if (!isBlank(l1[52]))
        return fail(ParseError::BAD_FORMAT, col(53) + " 必须是空格");

    // ---- B* 阻力项（列54-61） ----
    {
        if (!signOrBlank(l1[53]))
            return fail(ParseError::BAD_BS, col(54) + " 必须是 +、- 或空格");
        if (!allOf(l1, 54, 5, isDigit))
            return fail(ParseError::BAD_BS, "B* 尾数（列55-59）必须是数字");
        if (!signOrBlank(l1[59]))
            return fail(ParseError::BAD_BS, col(60) + " 必须是 +、- 或空格");
        if (!isDigit(l1[60]) && !isBlank(l1[60]))
            return fail(ParseError::BAD_BS, col(61) + " 必须是数字或空格");
        try {
            tle.bstar = parseScientific(l1.substr(53, 8));
        } catch (const std::invalid_argument &e) {
            // parseScientific 的全部失败路径均抛 invalid_argument:
            // 只捕获它, 让 bad_alloc 等资源类异常向上传播
            return fail(ParseError::BAD_BS,
                        std::string("Invalid B* format: ") + e.what());
        }
    }

    // ---- 空列62 / 星历类型（列63） / 空列64 / 元素集号（列65-68） ----
    if (!isBlank(l1[61]))
        return fail(ParseError::BAD_FORMAT, col(62) + " 必须是空格");
    if (!isDigit(l1[62]) && !isBlank(l1[62]))
        return fail(ParseError::BAD_FORMAT, "星历类型（列63）必须是数字或空格");
    if (!isBlank(l1[63]))
        return fail(ParseError::BAD_FORMAT, col(64) + " 必须是空格");
    {
        if (!allDigitOrBlank(l1, 64, 3))
            return fail(ParseError::BAD_FORMAT, "元素集号（列65-67）必须是数字或空格");
        if (!isDigit(l1[67]))
            return fail(ParseError::BAD_FORMAT, "元素集号（列68）必须是数字");
        int n = 0;
        if (numericField(l1, 64, 4, n)) tle.elsetNum = n;
    }
    if (isDigit(l1[62])) tle.tleEphemerisType = l1[62] - '0';
    // 注: 第 69 列（校验和）按 STK "No validation performed" 不做任何校验

    // ================= 行2 =================
    {
        // 编目号（列3-7）与行1 必须一致（STK 定列读取两个字段）
        const std::string sn2 = l2.substr(2, 5);
        if (!allOf(l2, 2, 4, isAlnumOrBlank) || !isAlnum(l2[6]))
            return fail(ParseError::BAD_NORAD, "编目号（行2 列3-7）格式非法");
        if (!tle.noradStr.empty()) {
            std::string a = tle.noradStr;
            std::string b = sn2;
            while (!b.empty() && b.front() == ' ') b.erase(0, 1);
            while (!b.empty() && b.back() == ' ')  b.pop_back();
            if (a != b)
                return fail(ParseError::BAD_NORAD,
                            "行1 与行2 的编目号不一致: '" + a + "' / '" + b + "'");
        }
    }
    if (!isBlank(l2[7]))
        return fail(ParseError::BAD_FORMAT, col(8) + " 必须是空格（行2）");

    // ---- 倾角（列9-16） ----
    {
        if (!allDigitOrBlank(l2, 8, 3))
            return fail(ParseError::BAD_ELEMENTS, "倾角整数部分（列9-11）必须是数字或空格");
        if (l2[11] != '.')
            return fail(ParseError::BAD_ELEMENTS, col(12) + " 必须是小数点");
        if (!allOf(l2, 12, 4, isDigit))
            return fail(ParseError::BAD_ELEMENTS, "倾角小数位（列13-16）必须是数字");
        if (!numericField(l2, 8, 8, tle.inclinationDeg))
            return fail(ParseError::BAD_ELEMENTS, "倾角解析失败");
        if (tle.inclinationDeg < 0.0 || tle.inclinationDeg > 180.0)
            return fail(ParseError::BAD_ELEMENTS,
                        "倾角超出 STK 允许范围 [0, 180]: "
                        + std::to_string(tle.inclinationDeg));
    }
    if (!isBlank(l2[16]))
        return fail(ParseError::BAD_FORMAT, col(17) + " 必须是空格（行2）");

    // ---- 升交点赤经（列18-25） ----
    {
        if (!allDigitOrBlank(l2, 17, 3))
            return fail(ParseError::BAD_ELEMENTS, "升交点赤经整数部分（列18-20）必须是数字或空格");
        if (l2[20] != '.')
            return fail(ParseError::BAD_ELEMENTS, col(21) + " 必须是小数点");
        if (!allOf(l2, 21, 4, isDigit))
            return fail(ParseError::BAD_ELEMENTS, "升交点赤经小数位（列22-25）必须是数字");
        if (!numericField(l2, 17, 8, tle.raanDeg))
            return fail(ParseError::BAD_ELEMENTS, "升交点赤经解析失败");
        if (tle.raanDeg < 0.0 || tle.raanDeg > 360.0)
            return fail(ParseError::BAD_ELEMENTS,
                        "升交点赤经超出 STK 允许范围 [0, 360]: "
                        + std::to_string(tle.raanDeg));
    }
    if (!isBlank(l2[25]))
        return fail(ParseError::BAD_FORMAT, col(26) + " 必须是空格（行2）");

    // ---- 偏心率（列27-33，隐含前导小数点） ----
    {
        if (!allOf(l2, 26, 7, isDigit))
            return fail(ParseError::BAD_ELEMENTS, "偏心率（列27-33）必须是 7 位数字");
        int eccInt = 0;
        if (!parseStrict(l2.substr(26, 7), eccInt))
            return fail(ParseError::BAD_ELEMENTS, "偏心率解析失败");
        tle.eccentricity = static_cast<double>(eccInt) / 1.0e7;
        if (!(tle.eccentricity >= 0.0 && tle.eccentricity < 1.0))
            return fail(ParseError::BAD_ELEMENTS,
                        "偏心率超出 [0, 1): " + std::to_string(tle.eccentricity));
    }
    if (!isBlank(l2[33]))
        return fail(ParseError::BAD_FORMAT, col(34) + " 必须是空格（行2）");

    // ---- 近地点辐角（列35-42） ----
    {
        if (!allDigitOrBlank(l2, 34, 3))
            return fail(ParseError::BAD_ELEMENTS, "近地点辐角整数部分（列35-37）必须是数字或空格");
        if (l2[37] != '.')
            return fail(ParseError::BAD_ELEMENTS, col(38) + " 必须是小数点");
        if (!allOf(l2, 38, 4, isDigit))
            return fail(ParseError::BAD_ELEMENTS, "近地点辐角小数位（列39-42）必须是数字");
        if (!numericField(l2, 34, 8, tle.argPerigeeDeg))
            return fail(ParseError::BAD_ELEMENTS, "近地点辐角解析失败");
        if (tle.argPerigeeDeg < 0.0 || tle.argPerigeeDeg > 360.0)
            return fail(ParseError::BAD_ELEMENTS,
                        "近地点辐角超出 STK 允许范围 [0, 360]: "
                        + std::to_string(tle.argPerigeeDeg));
    }
    if (!isBlank(l2[42]))
        return fail(ParseError::BAD_FORMAT, col(43) + " 必须是空格（行2）");

    // ---- 平近点角（列44-51） ----
    {
        if (!allDigitOrBlank(l2, 43, 3))
            return fail(ParseError::BAD_ELEMENTS, "平近点角整数部分（列44-46）必须是数字或空格");
        if (l2[46] != '.')
            return fail(ParseError::BAD_ELEMENTS, col(47) + " 必须是小数点");
        if (!allOf(l2, 47, 4, isDigit))
            return fail(ParseError::BAD_ELEMENTS, "平近点角小数位（列48-51）必须是数字");
        if (!numericField(l2, 43, 8, tle.meanAnomalyDeg))
            return fail(ParseError::BAD_ELEMENTS, "平近点角解析失败");
        if (tle.meanAnomalyDeg < 0.0 || tle.meanAnomalyDeg > 360.0)
            return fail(ParseError::BAD_ELEMENTS,
                        "平近点角超出 STK 允许范围 [0, 360]: "
                        + std::to_string(tle.meanAnomalyDeg));
    }
    if (!isBlank(l2[51]))
        return fail(ParseError::BAD_FORMAT, col(52) + " 必须是空格（行2）");

    // ---- 平运动（列53-63） ----
    {
        if (!allDigitOrBlank(l2, 52, 2))
            return fail(ParseError::BAD_ELEMENTS, "平运动整数部分（列53-54）必须是数字或空格");
        if (l2[54] != '.')
            return fail(ParseError::BAD_ELEMENTS, col(55) + " 必须是小数点");
        if (!allOf(l2, 55, 8, isDigit))
            return fail(ParseError::BAD_ELEMENTS, "平运动小数位（列56-63）必须是数字");
        if (!numericField(l2, 52, 11, tle.meanMotionRevDay))
            return fail(ParseError::BAD_ELEMENTS, "平运动解析失败");
        // STK 校验: 平运动不得大于 17.0
        if (!(tle.meanMotionRevDay > 0.0 && tle.meanMotionRevDay <= 17.0))
            return fail(ParseError::BAD_ELEMENTS,
                        "平运动超出 STK 允许范围 (0, 17.0]: "
                        + std::to_string(tle.meanMotionRevDay));
    }

    // ---- 圈号（列64-68） ----
    {
        if (!allDigitOrBlank(l2, 63, 4))
            return fail(ParseError::BAD_FORMAT, "圈号（列64-67）必须是数字或空格");
        if (!isDigit(l2[67]))
            return fail(ParseError::BAD_FORMAT, "圈号（列68）必须是数字");
        int rv = 0;
        if (numericField(l2, 63, 5, rv)) tle.revNumber = rv;
    }
    // 注: 行2 第 69 列（校验和）同样不校验

    // ---- 模型分类 ----
    tle.ephemerisType = classifyModel(tle.meanMotionRevDay);

    if (errOut) *errOut = err;   // code == OK
    return tle;
}

// 模型分类
// 与 Vallado SGP4/perturb 判据一致：轨道周期 ≥ 225 分钟（平运动 ≤ 6.4 rev/day）
// → 深空模型 SDP4；否则近地 SGP4
// 深空带内再细分: 周期 ≥ 1400 min（平运动 ≤ 1440/1400 ≈ 1.0286 rev/day）
// → 同步带/超同步 GEO（与 Propagator 的 no_unkozai 分类同阈值、同语义）
Sgp4Type TleParser::classifyModel(double meanMotionRevDay)
{
    if (meanMotionRevDay > 6.4)                        // 周期 < 225 分钟 → 近地
        return Sgp4Type::LEO_SGP4;
    else if (meanMotionRevDay >= 1440.0 / 1400.0)      // 周期 225 ~ 1400 分钟 → 深空/中轨
        return Sgp4Type::MEO_SDP4;
    else                                               // 周期 ≥ 1400 分钟 → 同步带/超同步
        return Sgp4Type::GEO_SDP4_DS;
}

// 科学记数法解析（行1 的 n/6、B* 共用的 8 列字段）
// 格式 "SmmmmmSE"（如 " 24655-3"）→ ±0.mmmmm × 10^E = 2.4655e-4
// 严格整段消费；长度不足 / 非法字符 / 指数越界（TLE 规范 ±9）→ 抛 invalid_argument
double TleParser::parseScientific(const std::string &s)
{
    try {
        if (s.length() < 8)
            throw std::invalid_argument("字段长度不足 8 列: " + std::to_string(s.length()));

        std::string mantissaStr = s.substr(0, 6);   // 符号/空格 + 5 位尾数
        std::string expStr = s.substr(6, 2);        // 指数符号 + 指数值

        for (char &c : mantissaStr)
            if (c == ' ') c = '0';
        if (mantissaStr[0] == ' ') mantissaStr[0] = '0';

        double mantissa = 0.0;
        int exponent = 0;
        if (!parseStrict(mantissaStr, mantissa))
            throw std::invalid_argument("尾数解析失败: '" + mantissaStr + "'");
        if (!parseStrict(expStr, exponent))
            throw std::invalid_argument("指数解析失败: '" + expStr + "'");
        if (exponent < -9 || exponent > 9)
            throw std::invalid_argument("指数超出 TLE 规范 [-9, +9]: " + std::to_string(exponent));

        double result = mantissa * 1e-5 * std::pow(10.0, exponent);  // 隐含前导小数点
        if (!std::isfinite(result))
            throw std::invalid_argument("结果非有限值");

        return result;
    } catch (const std::invalid_argument &e) {
        // 只加一层上下文并保持异常类型; bad_alloc 等资源类异常向上传播,
        // 不得被误分类为"字段格式错误"
        throw std::invalid_argument("字段解析失败 ('" + s + "'): " + e.what());
    }
}
