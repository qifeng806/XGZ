#ifndef CORE_TLEPARSER_H
#define CORE_TLEPARSER_H
// =============================================================================
// TleParser.h — TLE 两行根数解析器
// 按 CCSDS 标准解析 TLE，含校验和、范围检查和模型分类
// =============================================================================
#include "models.h"
#include <string>
#include <optional>

class TleParser {
public:
    struct ParseError {
        enum Code {
            OK = 0,
            BAD_LENGTH,
            BAD_LINE_NUMBER,
            CHECKSUM_FAILED,
            BAD_NORAD,
            BAD_EPOCH,
            BAD_ELEMENTS,
            BAD_BS,
            DECAYED
        };
        Code code = OK;
        std::string detail;
    };

    // 从三行解析（行0=名称，行1=TLE第一行，行2=TLE第二行）
    static std::optional<TleData>
        parse(const std::string &line0,
              const std::string &line1,
              const std::string &line2,
              ParseError *errOut = nullptr);

    // 从两行解析（无名称行，自动用 NORAD ID 作为名称）
    static std::optional<TleData>
        parse(const std::string &line1,
              const std::string &line2,
              ParseError *errOut = nullptr);

    // 根据平运动判定轨道模型
    static Sgp4Type classifyModel(double meanMotionRevDay);

    // 将科学记数法字符串（如 " 12345-3" → 0.12345e-3）转为 double
    static double parseScientific(const std::string &s);

    // 验证校验和（模 10）
    static bool verifyChecksum(const std::string &line);
};

#endif // CORE_TLEPARSER_H
