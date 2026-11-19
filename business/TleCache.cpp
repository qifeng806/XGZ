// =============================================================================
// TleCache.cpp — TLE 本地磁盘缓存实现
// =============================================================================
#include "TleCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// tag → 安全文件名: 仅保留 [A-Za-z0-9._-]，其余替换为 '_'
// （tag 来自组标识 "space-station"/"CATNR-25544" 等，仍需防御性净化，
//   避免路径分隔符/相对路径逃逸出缓存目录）
QString sanitizeTag(const QString &tag)
{
    QString out;
    out.reserve(tag.size());
    for (const QChar c : tag) {
        const ushort u = c.unicode();
        const bool ok = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')
                     || (u >= '0' && u <= '9') || u == '.' || u == '_' || u == '-';
        out.append(ok ? c : QChar('_'));
    }
    if (out.isEmpty()) out = QStringLiteral("_");
    return out;
}

} // namespace

QString TleCache::cacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/tle_cache");
}

QString TleCache::cachePath(const QString &tag)
{
    return cacheDir() + QStringLiteral("/") + sanitizeTag(tag) + QStringLiteral(".3le");
}

bool TleCache::read(const QString &tag, QString &text, qint64 &ageSec)
{
    text.clear();
    ageSec = -1;
    if (tag.isEmpty()) return false;

    const QString path = cachePath(tag);
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile() || fi.size() <= 0) return false;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QByteArray raw = f.readAll();
    f.close();

    const QString cached = QString::fromUtf8(raw);
    if (cached.trimmed().isEmpty()) return false;

    // 时间戳异常（未来时间/时钟回拨）→ ageSec < 0, 调用方按 "不新鲜" 处理
    const QDateTime mtime = fi.lastModified();
    if (mtime.isValid())
        ageSec = mtime.secsTo(QDateTime::currentDateTimeUtc());
    else
        ageSec = -1;

    text = cached;
    return true;
}

void TleCache::write(const QString &tag, const QString &text)
{
    if (tag.isEmpty() || text.trimmed().isEmpty()) return;

    QDir dir(cacheDir());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return;

    // QSaveFile: 先写临时文件再原子替换 —— 直接 Truncate 写会在
    // 进程被杀/磁盘满时留下**半截缓存**, 下次启动会被当成有效数据解析
    // （表现为"下载成功却少了若干颗卫星"）。commit() 失败则丢弃临时文件,
    // 旧缓存保持不变。
    QSaveFile f(cachePath(tag));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    if (f.write(text.toUtf8()) < 0) { f.cancelWriting(); return; }
    f.commit();
}
