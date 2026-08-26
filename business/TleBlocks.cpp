// =============================================================================
// TleBlocks.cpp — 多颗 TLE 文本块解析实现
// =============================================================================
#include "TleBlocks.h"

QVector<TleBlock> parseTleBlocks(const QString &text, int *incompleteCount)
{
    QVector<TleBlock> blocks;
    QString pendingName;
    QString pendingL1;
    int incomplete = 0;

    // TLE 行严格判据: 前缀 "1 "/"2 " 且长度 ≥ 69。
    // 避免把 "1U CUBESAT"、"1999-xxx 论文标题" 之类名称行误判为 TLE 行
    const auto isTleLine1 = [](const QString &l) {
        return l.startsWith("1 ") && l.length() >= 69;
    };
    const auto isTleLine2 = [](const QString &l) {
        return l.startsWith("2 ") && l.length() >= 69;
    };

    const QStringList lines = text.split('\n');
    for (const QString &raw : lines) {
        const QString l = raw.trimmed();
        if (l.isEmpty()) continue;
        // 注释行跳过（批量导入对话框承诺「含注释头时自动忽略」）:
        // 若当作名称行, 「名称→注释→行1」序列会把卫星名替换为注释文本,
        // 污染表格显示/身份键/导出
        if (l.startsWith('#')) continue;
        if (isTleLine1(l)) {
            if (!pendingL1.isEmpty()) incomplete++;  // 上一颗只有行1 → 半截块
            pendingL1 = l;
        } else if (isTleLine2(l)) {
            if (!pendingL1.isEmpty()) {
                blocks.append({ pendingName, pendingL1, l });
                pendingName.clear();
                pendingL1.clear();
            } else {
                incomplete++;   // 只有行2 → 半截块
            }
        } else {
            pendingName = l;   // 非 TLE 行视为下一颗卫星的名称
        }
    }
    if (!pendingL1.isEmpty()) incomplete++;   // 结尾残留单行1
    if (incompleteCount) *incompleteCount = incomplete;
    return blocks;
}
