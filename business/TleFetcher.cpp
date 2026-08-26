// =============================================================================
// TleFetcher.cpp — CelesTrak 在线 TLE 下载实现
// =============================================================================
#include "TleFetcher.h"
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QTimer>

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
    m_timeoutTimer.reset();   // 请求已中止, 看门狗失效
    emit finished();
}

void TleFetcher::startRequest(const QUrl &url, const QString &tag)
{
    // 新请求开始即解除历史取消状态（防残留标志吞掉本次正常结果）
    m_cancelled = false;
    if (m_reply) {
        // Qt6 的 QNetworkReply::abort() 会同步发射 finished() 重入
        // onReplyFinished（QCoro/Qt bug 报告已证实, 非异步）——必须先把
        // m_reply 摘为 nullptr 再 abort, 否则 onReplyFinished 内 m_reply=nullptr
        // 返回后此处继续解引用即空指针崩溃; 同时置取消标志让旧请求的收尾
        // 走静默清理分支（否则 aborted 回复会走正常错误路径弹虚假"网络错误"）
        QNetworkReply *old = m_reply;
        m_reply = nullptr;
        m_cancelled = true;
        old->abort();
        old->deleteLater();
    }
    m_cancelled = false;   // 旧请求已中止/标记丢弃, 新请求恢复正常语义

    QNetworkRequest req(url);
    req.setTransferTimeout(30000);
    // CelesTrak 若返回 3xx（域名/路径调整），自动跟随，避免"未解析到有效
    // TLE"的误导性错误（仅允许降级到同样安全或更安全的策略）
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
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

    // 启动超时看门狗（单发 30s, 与 setTransferTimeout 同量级）:
    // DNS 解析/建连挂起时 transferTimeout 在部分平台不触发, 看门狗兜底
    if (!m_timeoutTimer) {
        m_timeoutTimer = std::make_unique<QTimer>(this);
        m_timeoutTimer->setSingleShot(true);
        m_timeoutTimer->setInterval(30000);
        connect(m_timeoutTimer.get(), &QTimer::timeout, this, [this]() {
            if (!m_reply || m_cancelled) return;   // 已收尾/已在取消路径
            if (m_reply->isFinished()) return;     // 数据已到, finished 已排队, 看门狗退场
            m_cancelled = true;                    // aborted 回复走静默清理分支
            m_reply->abort();
            emit fetchError("网络请求超时（30 秒未完成，请检查网络后重试）");
            emit finished();
        });
    }
    m_timeoutTimer->start();
}

void TleFetcher::onReplyFinished(QNetworkReply *reply)
{
    // 取消请求或已被新请求取代 → 静默忽略（不弹"网络错误"误报）
    if (m_cancelled) {
        // 双取消竞态: 本分支只清自身状态。旧取消请求的迟到 finished
        // 若在此被处理（m_reply 可能已指向新一轮请求）就无条件清除
        // m_cancelled/m_pendingTag, 会让紧随其后的第二轮取消 aborted
        // 回复误走"正常错误路径"弹出虚假的"网络错误"对话框。
        // → 仅当这是当前活动回复时才复位全局状态
        if (m_reply == reply) {
            m_cancelled = false;
            m_reply = nullptr;
            m_pendingTag.clear();
            // 本函数可能在 timeout→abort→finished 的同步重入链内被调用:
            // 此时销毁发送者 QTimer 属"槽内删除信号发送者"高风险模式
            //（Qt6 连接数据 COW 通常兜住, 非保证）——延迟到下一轮事件循环。
            // 指针相等守卫: 若延迟期间 startRequest 已为新一轮请求重建了
            // 看门狗, 不得把活动定时器误杀（否则新请求失去超时保护）
            QTimer *timer = m_timeoutTimer.get();
            QTimer::singleShot(0, this, [this, timer]() {
                if (m_timeoutTimer.get() == timer) m_timeoutTimer.reset();
            });
        }
        reply->deleteLater();   // 修复: 取消分支此前漏删 reply，反复取消会累积泄漏
        return;
    }
    if (m_reply != reply) {
        reply->deleteLater();   // 迟到的旧请求，直接丢弃
        return;
    }
    m_reply = nullptr;
    reply->deleteLater();
    m_timeoutTimer.reset();   // 当前请求已收尾, 看门狗失效

    const QString tag = m_pendingTag;
    m_pendingTag.clear();

    const QNetworkReply::NetworkError err = reply->error();
    if (err != QNetworkReply::NoError) {
        // 附带 HTTP 状态码（CelesTrak 有速率限制 429, 便于用户区分限流/服务端错）
        const int httpCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        emit fetchError(httpCode > 0
            ? QString("网络错误(HTTP %1): %2").arg(httpCode).arg(reply->errorString())
            : QString("网络错误: %1").arg(reply->errorString()));
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
