#ifndef CORE_TLEPARSER_H
#define CORE_TLEPARSER_H
// =============================================================================
// TleParser.h — TLE 两行根数解析器
//
// 解析口径以 **STK 11** 为基准，逐字段对应 STK 帮助
//   "Create & Import External Files > TLE File Format (*.tce /*.tle)"
//   （本机 D:\AGI Systems Tool Kit(STK)\Help\Content\stk\importfiles-11.htm）
//
// STK 的两条硬性约定（本实现严格遵守）:
//   1. **定列读取**（fixed-read）: "STK uses a fixed-read format, reading the
//      specific columns as indicated in the TLE format, for extracting all of
//      the TLE data." → 本解析器只按列号取值，绝不做"按空格分词再重排列"
//      之类的对齐修复（那会读出与 STK 不同的根数）。
//   2. **列格式校验**（TLE Validator，本机 _Default.ap 中 Validator = On）:
//      空列必须是空格、小数点必须在指定列、数字列必须是数字、数值必须落在
//      STK 给定范围内（倾角 0~180、RAAN/近地点辐角/平近点角 0~360、
//      平运动 ≤ 17.0、历元日整数部分 ≤ 366）。不满足即拒绝。
//   3. 第 69 列（校验和）: STK 帮助明确 "No validation performed" →
//      本解析器**不校验校验和**（不做模 10 计算，也不因校验和不符而拒绝）。
//
// 列定义（1 基，与 STK 帮助表格逐列一致）:
//   行1: 1=卡号'1' 2=空 3-7=编目号 8=密级 9=空 10-17=国际编号 18=空
//        19-20=历元年(YY) 21-32=历元日(DDD.DDDDDDDD) 33=空
//        34-43=n/2(rev/day²) 44=空 45-52=n/6(rev/day³) 53=空
//        54-61=B*(1/Re) 62=空 63=星历类型 64=空 65-68=元素集号 69=校验和
//   行2: 1=卡号'2' 2=空 3-7=编目号 8=空 9-16=倾角 17=空 18-25=升交点赤经
//        26=空 27-33=偏心率(隐含小数点) 34=空 35-42=近地点辐角 43=空
//        44-51=平近点角 52=空 53-63=平运动 64-68=圈号 69=校验和
// =============================================================================
#include "DataTypes.h"
#include <string>
#include <optional>
#include <utility>

// TLE 标准行长度（CCSDS 两行根数格式）
constexpr int kTleLineLength = 69;

class TleParser {
public:
    struct ParseError {
        enum Code {
            OK = 0,
            BAD_LENGTH,        // 行长不足 69 列（STK 定列读取的最小长度）
            BAD_LINE_NUMBER,   // 行首卡号不是 '1' / '2'
            BAD_FORMAT,        // 列格式不符（空列非空/数字列非数字/小数点错位）
            BAD_NORAD,         // 编目号列非法
            BAD_EPOCH,         // 历元列非法（格式或范围）
            BAD_ELEMENTS,      // 轨道根数格式或范围非法
            BAD_BS,            // B* 列非法
            DECAYED            // 保留枚举: 再入由 Propagator/perturb 在初始化阶段报出
        };
        Code code = OK;
        std::string detail;
    };

    // 从三行解析（行0=名称，行1=TLE第一行，行2=TLE第二行）
    // 从两行解析（无名称行，自动用编目号作为名称）
    //
    // 调用约定: **返回值优先于 errOut**——解析成功即返回数据; errOut 仅在
    // 返回 nullopt 时携带失败原因。
    static std::optional<TleData>
        parse(const std::string &line0,
              const std::string &line1,
              const std::string &line2,
              ParseError *errOut = nullptr);

    static std::optional<TleData>
        parse(const std::string &line1,
              const std::string &line2,
              ParseError *errOut = nullptr);

    // 根据平运动判定轨道模型（SGP4/SDP4 分类，仅用于展示标签）
    static Sgp4Type classifyModel(double meanMotionRevDay);

    // 科学记数法字段解析（行1 的 n/6 与 B* 共用）:
    //   "SmmmmmSE"（列45-52 / 列54-61）→ ±0.mmmmm × 10^E
    // 例: " 24655-3" → +0.24655e-3 = 2.4655e-4
    static double parseScientific(const std::string &s);

    // 仅剥除行尾 CR/LF（定列读取不需要、也不允许其它"规范化"）
    static std::pair<std::string, std::string>
        trimLineEndings(const std::string &line1, const std::string &line2);
};

#endif // CORE_TLEPARSER_H
