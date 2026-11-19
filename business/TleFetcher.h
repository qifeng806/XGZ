#ifndef BUSINESS_TLEFETCHER_H
#define BUSINESS_TLEFETCHER_H
// =============================================================================
// TleFetcher.h — CelesTrak 在线 TLE 下载器
// 使用公开 API: https://celestrak.org/NORAD/elements/gp.php
//   组下载: ?GROUP=<id>&FORMAT=tle    单星: ?CATNR=<编号>&FORMAT=tle
// 返回 3LE 文本（名称行 + 两行根数），与批量导入解析器直接兼容
// 注意: 请遵守 CelesTrak 数据使用政策，避免高频轮询
// =============================================================================
#include <QObject>
#include <QVector>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QTimer>
#include <memory>

class QTimer;

class TleFetcher : public QObject {
    Q_OBJECT
public:
    struct Group {
        QString id;      // API 组标识
        QString label;   // 中文显示名
    };

    // CelesTrak 常用 GP 组
    static const QVector<Group> &groups();

    explicit TleFetcher(QObject *parent = nullptr);

    // 下载一组卫星的 3LE 文本（异步）
    // 命中 2 小时内的本地缓存则直接复用，不发网络请求
    //   （CelesTrak 每 ~2 小时才更新，重复请求会 403 甚至被封 IP）
    void fetchGroup(const QString &groupId);

    // 忽略本地缓存强制联网（UI 的 Ctrl+点击 / 右键菜单入口专用）。
    // 注意: CelesTrak 对同一组数据在 2 小时内只允许拉取一次，
    // 强制刷新可能直接收到 HTTP 403（UI 会给出专门提示）
    void refetchGroup(const QString &groupId);

    // 下载单个编目号卫星的 3LE 文本（异步；缓存语义同 fetchGroup）
    void fetchSatellite(int catnorID);

    void cancel();

signals:
    // 下载成功: 原始 3LE 文本（UTF-8）
    void downloaded(QString rawText, QString groupId);
    // 命中本地缓存（未发网络请求）: tag 与缓存年龄（秒），供 UI 提示数据新鲜度
    void cacheServed(QString tag, qint64 ageSec);
    // 下载失败（网络错误/空数据）
    void fetchError(QString message);
    // 请求结束（无论成败，供 UI 复位状态）
    void finished();

private:
    void startRequest(const QUrl &url, const QString &tag);
    void onReplyFinished(QNetworkReply *reply);

    // 缓存命中时的延迟投递（保持 downloaded/finished 的异步时序，
    // 避免在按钮点击处理栈内同步弹出模态对话框）
    bool tryServeFromCache(const QString &tag);

    // fetchGroup / refetchGroup 共用实现（bypassCache=true 时忽略本地缓存）
    void fetchGroupImpl(const QString &groupId, bool bypassCache);

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QString m_pendingTag;
    bool m_cancelled = false;   // 取消请求后静默忽略迟到回复
    bool m_cachePending = false;   // 已排队的缓存投递（cancel 需能撤销）
    QString m_cacheTag;            // 排队投递对应的 tag（被新请求取代时作废）
    // 超时看门狗: setTransferTimeout 在部分平台对 DNS 解析/建连挂起不生效,
    // 用单发定时器兜底——到期仍未完成即中止并以超时错误上报
    std::unique_ptr<QTimer> m_timeoutTimer;
    std::size_t m_timerGeneration{0};   // 延迟重置回调的世代守卫（防地址复用误杀新 timer）
};

#endif // BUSINESS_TLEFETCHER_H
