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
    void fetchGroup(const QString &groupId);

    // 下载单个编目号卫星的 3LE 文本（异步）
    void fetchSatellite(int catnorID);

    void cancel();

signals:
    // 下载成功: 原始 3LE 文本（UTF-8）
    void downloaded(QString rawText, QString groupId);
    // 下载失败（网络错误/空数据）
    void fetchError(QString message);
    // 请求结束（无论成败，供 UI 复位状态）
    void finished();

private:
    void startRequest(const QUrl &url, const QString &tag);
    void onReplyFinished(QNetworkReply *reply);

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QString m_pendingTag;
    bool m_cancelled = false;   // 取消请求后静默忽略迟到回复
    // 超时看门狗: setTransferTimeout 在部分平台对 DNS 解析/建连挂起不生效,
    // 用单发定时器兜底——到期仍未完成即中止并以超时错误上报
    QTimer *m_timeoutTimer = nullptr;
};

#endif // BUSINESS_TLEFETCHER_H
