// =============================================================================
// TleFetcher.cpp — CelesTrak 在线 TLE 下载实现
// =============================================================================
#include "TleFetcher.h"
#include <QNetworkRequest>
#include <QUrlQuery>

const QVector<TleFetcher::Group> &TleFetcher::groups()
{
    static const QVector<Group> g = {
        { "space-station", "空间站" },
        { "visual",        "最亮 100 颗" },
        { "stations",      "常用业余卫星" },
        { "active",        "全部活动卫星" },
        { "starlink",      "星链 Starlink" },
        { "oneweb",        "一网 OneWeb" },
        { "gps-ops",       "GPS 星座" },
        { "glo-ops",       "GLONASS 星座" },
        { "gal-ops",       "Galileo 星座" },
        { "beidou",        "北斗星座" },
        { "geo",           "地球同步轨道" },
        { "weather",       "气象卫星" },
        { "science",       "科学卫星" },
        { "cubesat",       "立方星" },
        { "last-30-days",  "近 30 天发射" },
    };
    return g;
}

TleFetcher::TleFetcher(QObject *parent)
    : QObject(parent)
{
}

void TleFetcher::fetchGroup(const QString &groupId)
{
    QUrlQuery q;
    q.addQueryItem("GROUP", groupId);
    q.addQueryItem("FORMAT", "tle");
    QUrl url("https://celestrak.org/NORAD/elements/gp.php");
    url.setQuery(q);
    startRequest(url, groupId);
}

void TleFetcher::fetchSatellite(int catnorID)
{
    QUrlQuery q;
    q.addQueryItem("CATNR", QString::number(catnorID));
    q.addQueryItem("FORMAT", "tle");
    QUrl url("https://celestrak.org/NORAD/elements/gp.php");
    url.setQuery(q);
    startRequest(url, QString("CATNR-%1").arg(catnorID));
}

void TleFetcher::cancel()
{
    // 仅在存在活动请求时置取消标志: 无请求时置位会让下一次正常下载
    // 被 onReplyFinished 的静默分支误吞（数据丢弃且不弹框）
    if (m_reply) {
        m_cancelled = true;
        m_reply->abort();
    }
    emit finished();
}

void TleFetcher::startRequest(const QUrl &url, const QString &tag)
{
    // 新请求开始即解除历史取消状态（防残留标志吞掉本次正常结果）
    m_cancelled = false;
    if (m_reply) m_reply->deleteLater();   // 并发保护: 丢弃旧请求

    QNetworkRequest req(url);
    req.setTransferTimeout(30000);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("XGZ-PassPredictor/1.0 (Qt %1)").arg(QT_VERSION_STR));

    m_pendingTag = tag;
    QNetworkReply *reply = m_nam.get(req);
    m_reply = reply;
    // 捕获 reply 指针而非依赖 m_reply: 防止旧请求的 finished 迟到时
    // 误处理已被新请求替换的 m_reply（并发竞态）
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply);
    });
}

void TleFetcher::onReplyFinished(QNetworkReply *reply)
{
    // 取消请求或已被新请求取代 → 静默忽略（不弹"网络错误"误报）
    if (m_cancelled) {
        m_cancelled = false;
        if (m_reply == reply) m_reply = nullptr;
        reply->deleteLater();   // 修复: 取消分支此前漏删 reply，反复取消会累积泄漏
        return;
    }
    if (m_reply != reply) {
        reply->deleteLater();   // 迟到的旧请求，直接丢弃
        return;
    }
    m_reply = nullptr;
    reply->deleteLater();

    const QString tag = m_pendingTag;
    m_pendingTag.clear();

    const QNetworkReply::NetworkError err = reply->error();
    if (err != QNetworkReply::NoError) {
        emit fetchError(QString("网络错误: %1").arg(reply->errorString()));
        emit finished();
        return;
    }

    const QByteArray data = reply->readAll();
    const QString text = QString::fromUtf8(data).trimmed();
    if (text.isEmpty()) {
        emit fetchError("服务器未返回数据（请检查组名或稍后重试）");
        emit finished();
        return;
    }
    emit downloaded(text, tag);
    emit finished();
}
