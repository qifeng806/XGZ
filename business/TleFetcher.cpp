// =============================================================================
// TleFetcher.cpp — CelesTrak 在线 TLE 下载实现
// 本地缓存: 2 小时内的成功下载直接复用（见 business/TleCache），
// 既省流量也规避 CelesTrak 的"每次更新只允许下载一次"限流（HTTP 403）
// =============================================================================
#include "TleFetcher.h"
#include "TleCache.h"
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QTimer>

// CelesTrak 网络请求超时（毫秒）及看门狗间隔
static constexpr int kNetworkTimeoutMs = 30000;

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
    fetchGroupImpl(groupId, false /* bypassCache */);
}

void TleFetcher::refetchGroup(const QString &groupId)
{
    fetchGroupImpl(groupId, true /* bypassCache */);
}

void TleFetcher::fetchGroupImpl(const QString &groupId, bool bypassCache)
{
    // 强制刷新: 显式绕过缓存（tryServeFromCache 内部亦有 tag 空值/在途保护）
    if (!bypassCache && tryServeFromCache(groupId)) return;

    QUrlQuery q;
    q.addQueryItem("GROUP", groupId);
    q.addQueryItem("FORMAT", "tle");
    QUrl url("https://celestrak.org/NORAD/elements/gp.php");
    url.setQuery(q);
    startRequest(url, groupId);
}

void TleFetcher::fetchSatellite(int catnorID)
{
    const QString tag = QString("CATNR-%1").arg(catnorID);
    if (tryServeFromCache(tag)) return;

    QUrlQuery q;
    q.addQueryItem("CATNR", QString::number(catnorID));
    q.addQueryItem("FORMAT", "tle");
    QUrl url("https://celestrak.org/NORAD/elements/gp.php");
    url.setQuery(q);
    startRequest(url, tag);
}

// 缓存优先: 新鲜（≤2 小时）缓存直接复用, 不发网络请求。
// 延迟到下一轮事件循环投递, 保持 downloaded/finished 的异步时序
//（否则会在按钮点击栈内同步弹出模态批量对话框, 且 finished 的
// 入口复位逻辑与调用栈交错）
bool TleFetcher::tryServeFromCache(const QString &tag)
{
    if (tag.isEmpty()) return false;
    // 在途保护:
    //  - 同一 tag 的投递已排队 → 直接复用（不再排队, 也不发网络请求）
    //  - 已有网络请求在途 → 返回 false, 交调用方按"新请求替换旧请求"处理
    //    （startRequest 会中止旧回复并作废排队投递, 语义与改动前一致）
    if (m_cachePending && m_cacheTag == tag) return true;
    if (m_reply != nullptr) return false;

    QString cached;
    qint64 ageSec = 0;
    if (!TleCache::read(tag, cached, ageSec) || !TleCache::isFresh(ageSec))
        return false;

    m_cachePending = true;
    m_cacheTag = tag;   // 记住本次排队投递的 tag: 后续请求会覆盖它并作废本投递
    QTimer::singleShot(0, this, [this, tag, cached, ageSec]() {
        // 期间被 cancel() 撤销、被新一轮请求取代（tag 不同）→ 静默丢弃,
        // 保证"投递的数据"与"最后一次请求的 tag"始终一致
        if (!m_cachePending || m_cacheTag != tag) return;
        m_cachePending = false;
        m_cacheTag.clear();
        emit cacheServed(tag, ageSec);
        emit downloaded(cached, tag);
        emit finished();
    });
    return true;
}

void TleFetcher::cancel()
{
    // 仅在存在活动请求（在途回复或已排队的缓存投递）时置取消标志:
    // 无请求时置位会让下一次正常下载被 onReplyFinished 的静默分支误吞
    const bool hadPendingCache = m_cachePending;
    m_cachePending = false;   // 撤销排队中的缓存投递（其回调自查该标志）
    m_cacheTag.clear();
    const bool hadReply = (m_reply != nullptr);
    if (m_reply) {
        // 顺序与 startRequest 一致（先摘 m_reply 再 abort）: Qt6 的 abort() 会
        // 同步发射 finished() 重入 onReplyFinished, 若此时 m_reply 仍指向该
        // 回复, 取消分支会顺带复位 m_cancelled / 清 m_pendingTag, 本函数后续
        // 语句便依赖"重入恰好发生过"这一实现细节; 先摘指针则重入必然走
        // "迟到的旧请求 → deleteLater" 分支, 两条路径都不泄露回复对象
        QNetworkReply *old = m_reply;
        m_reply = nullptr;
        m_cancelled = true;
        old->abort();
        m_cancelled = false;    // 回复已摘除: 无在途请求, 解除取消态
        m_pendingTag.clear();   // 归属该回复的 tag 一并作废
    }
    m_timeoutTimer.reset();         // 请求已中止, 看门狗失效
    // 无条件通知: 保证调用者收到 finished 信号（在途回复或被撤销的缓存投递都算活动请求）
    if (hadReply || hadPendingCache) emit finished();
}

void TleFetcher::startRequest(const QUrl &url, const QString &tag)
{
    // 新请求开始即解除历史取消状态（防残留标志吞掉本次正常结果）
    m_cancelled = false;
    // 作废排队中的缓存投递: 同一时刻只允许一次投递在途（否则"缓存命中"后
    // 紧接着发起新请求会让同一份 downloaded 被投递两次）
    m_cachePending = false;
    m_cacheTag.clear();
    if (m_reply) {
        // Qt6 的 QNetworkReply::abort() 会同步发射 finished() 重入
        // onReplyFinished（QCoro/Qt bug 报告已证实, 非异步）——必须先把
        // m_reply 摘为 nullptr 再 abort, 否则 onReplyFinished 内 m_reply=nullptr
        // 返回后此处继续解引用即空指针崩溃; 同时置取消标志让旧请求的收尾
        // 走静默清理分支（否则 aborted 回复会走正常错误路径弹虚假"网络错误"）
        // 注意: 不调用 old->deleteLater() —— onReplyFinished 的取消分支
        // (line 151) 已负责 reply->deleteLater(), 双重 deleteLater 会导致堆损坏
        QNetworkReply *old = m_reply;
        m_reply = nullptr;
        m_cancelled = true;
        old->abort();
    }
    m_cancelled = false;   // 旧请求已中止/标记丢弃, 新请求恢复正常语义

    // 销毁旧看门狗: 延迟单发回调 (onReplyFinished cancelled 分支) 可能在
    // 事件循环中排队等待重置旧 timer; 若此处复用旧 timer 对象, 延迟回调
    // 的指针相等守卫会误杀新请求的看门狗 → 请求永久挂起。先 reset 使延迟
    // 回调找到 nullptr != old_timer → no-op, 再创建新 timer
    m_timeoutTimer.reset();

    QNetworkRequest req(url);
    req.setTransferTimeout(kNetworkTimeoutMs);
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
        m_timeoutTimer->setInterval(kNetworkTimeoutMs);
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
    ++m_timerGeneration;   // 新请求看门狗已就绪，递增世代使旧延迟回调失效
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
            // 指针+世代双守卫: 若延迟期间 startRequest 已重建看门狗,
            // 即使分配器复用同一地址（指针相等），世代号不等也会跳过误杀
            QTimer *timer = m_timeoutTimer.get();
            std::size_t gen = m_timerGeneration;
            QTimer::singleShot(0, this, [this, timer, gen]() {
                // 注意: 本回调可能被"随后的模态弹窗"(onTleError 的 QMessageBox)
                // 打开的**嵌套事件循环**提前执行, 而那正是 QTimer::timeout 的
                // 发射栈——此时 delete 发送者是危险模式。改用 deleteLater():
                // DeferredDelete 事件不会被内层事件循环提前投递
                if (m_timeoutTimer.get() == timer && m_timerGeneration == gen)
                    m_timeoutTimer.release()->deleteLater();
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
    m_timeoutTimer.reset();   // 当前请求已收尾, 看门狗失效

    // 先读取全部数据再调度 deleteLater，避免嵌套事件循环下悬垂指针
    const QString tag = m_pendingTag;
    m_pendingTag.clear();

    const QNetworkReply::NetworkError err = reply->error();
    if (err != QNetworkReply::NoError) {
        const int httpCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString errorMsg = reply->errorString();
        reply->deleteLater();
        // 限流/无数据给出可操作的专门提示（CelesTrak 使用政策 + 格式限制）
        if (httpCode == 403) {
            emit fetchError(
                "CelesTrak 暂时拒绝下载（HTTP 403）\n\n"
                "官方政策：GP 数据每 ~2 小时才更新一次，且同一组数据在两次更新之间\n"
                "只允许下载一次；重复请求会被临时封禁。\n"
                "建议：本程序对 2 小时内的成功下载自动复用本地缓存（状态栏会提示\n"
                "\"使用本地缓存\"），稍后再试即可。");
        } else if (httpCode == 404) {
            emit fetchError(
                "CelesTrak 未返回数据（HTTP 404）\n\n"
                "该编目号/组当前没有可用的 GP 数据。注意：TLE 文本格式不支持\n"
                "6 位编目号（≥100000）的目标，此类对象只能通过 OMM（CSV/JSON/XML）获取。");
        } else {
            emit fetchError(httpCode > 0
                ? QString("网络错误(HTTP %1): %2").arg(httpCode).arg(errorMsg)
                : QString("网络错误: %1").arg(errorMsg));
        }
        emit finished();
        return;
    }

    const QByteArray data = reply->readAll();
    const QString text = QString::fromUtf8(data).trimmed();
    reply->deleteLater();
    if (text.isEmpty()) {
        emit fetchError("服务器未返回数据（请检查组名或稍后重试）");
        emit finished();
        return;
    }
    // 成功数据落盘缓存: 供 2 小时内的重复请求复用（也便于限流期间继续工作）
    TleCache::write(tag, text);
    emit downloaded(text, tag);
    emit finished();
}
