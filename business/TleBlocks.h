#ifndef BUSINESS_TLEBLOCKS_H
#define BUSINESS_TLEBLOCKS_H
// =============================================================================
// TleBlocks.h — 多颗 TLE 文本块解析（Qt 层）
// 支持 CelesTrak / NORAD 下载的 3LE 格式: [名称行] + 行1 + 行2
// 严格判据: 行1 以 "1 " 开头且长度 ≥ 69, 行2 以 "2 " 开头且长度 ≥ 69
// 名称行可选; 注释/说明行会被当作名称并被下一对 TLE 覆盖（无害）
// =============================================================================
#include <QString>
#include <QVector>

struct TleBlock {
    QString name;    // 名称行（可能为空）
    QString line1;
    QString line2;
};

// 解析多颗 TLE 文本块
// incompleteCount(可选出参): 半截块数量（只有行1/行2 而无配对者，静默丢失的源头）
QVector<TleBlock> parseTleBlocks(const QString &text, int *incompleteCount = nullptr);

#endif // BUSINESS_TLEBLOCKS_H
