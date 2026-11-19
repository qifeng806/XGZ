#ifndef BUSINESS_TLECACHE_H
#define BUSINESS_TLECACHE_H
// =============================================================================
// TleCache.h — TLE 下载结果的本地磁盘缓存
//
// 目的:
//   1) 避免重复下载。CelesTrak 的 GP 数据每 ~2 小时才更新一次，且自 2026-03
//      起对 Active/Starlink 等大组强制"每次更新只允许下载一次"，重复请求会
//      返回 HTTP 403；单日超量请求会被加入防火墙（官方使用政策）。
//   2) 限流/离线时仍可使用最近一次成功下载的数据。
//
// 设计:
//   - 缓存位置: QStandardPaths::AppDataLocation/tle_cache/<tag>.3le
//   - 以文件 mtime 作为"下载时间"，无需额外索引文件
//   - TTL 默认 2 小时（kTtlSec），与 CelesTrak 更新周期一致
//   - 纯 QtCore 实现（可被单元测试直接链接，无需 network 模块）
// =============================================================================
#include <QString>
#include <QtGlobal>

class TleCache {
public:
    // 缓存有效期（秒）: CelesTrak GP 数据更新周期约 2 小时
    static constexpr qint64 kTtlSec = 2 * 3600;

    // 缓存目录（不存在时按需创建）
    static QString cacheDir();

    // 某 tag 对应的缓存文件路径
    static QString cachePath(const QString &tag);

    // 读取缓存。命中（文件存在且非空）返回 true，并给出文本与
    // "距上次下载的秒数"（ageSec，<0 表示时间戳异常）
    static bool read(const QString &tag, QString &text, qint64 &ageSec);

    // 写入缓存（覆盖）。失败静默——缓存不是关键路径，绝不因此中断下载流程
    static void write(const QString &tag, const QString &text);

    // 缓存是否新鲜（ageSec ≤ TTL）
    static bool isFresh(qint64 ageSec) { return ageSec >= 0 && ageSec <= kTtlSec; }
};

#endif // BUSINESS_TLECACHE_H
