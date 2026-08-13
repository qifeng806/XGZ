#ifndef BUSINESS_PASSPREDICTORSERVICE_H
#define BUSINESS_PASSPREDICTORSERVICE_H
// =============================================================================
// PassPredictorService.h — 预报调度中心（单星和批量模式）
// 管理 PassPredictWorker 的生命周期和线程调度
// =============================================================================
#include <QObject>
#include <QPointer>
#include <QVector>
#include <QDateTime>
#include <memory>
#include "core/models.h"
#include "PassPredictWorker.h"

class QTimer;

class PassPredictorService : public QObject {
    Q_OBJECT
public:
    explicit PassPredictorService(QObject *parent = nullptr);
    ~PassPredictorService() override;

    // 单星预报
    void startJob(const PassPredictWorker::Job &job);

    // 批量预报（多颗卫星）
    void startBatch(const QVector<PassPredictWorker::Job> &jobs);

    // 取消当前所有计算
    void cancelAll();

    // 清理所有任务线程。
    // waitTimeoutMs: 单个线程的最长等待时间（毫秒）；>0 时超时后放弃等待
    //（让线程在后台自然后退，避免析构路径卡死主线程），≤0 表示无限等待
    void cleanupAll(int waitTimeoutMs = -1);

signals:
    void resultReady(QVector<PassEvent> passes);
    void errorOccurred(QString message);
    void batchProgress(int completed, int total);
    void metaInfo(QString model, QString epochUtc, double ageDays);

private:
    struct ActiveJob {
        QThread *thread = nullptr;
        QPointer<PassPredictWorker> worker;   // QPointer: worker 销毁后自动置空，避免悬垂
    };
    QVector<ActiveJob> m_activeJobs;

    // 批量模式汇总状态（成员变量，避免 lambda 捕获悬垂引用）
    QVector<PassEvent> m_batchPasses;   // 已完成的各卫星过境
    int  m_batchCompleted = 0;          // 已完成（成功+失败）任务数
    int  m_batchTotal     = 0;          // 批量任务总数
    bool m_batchFinished  = false;      // 本批已收尾（resultReady 发出前已置位）,
                                        // 迟到的单星 metaInfo 不再转发
    int  m_batchSeq       = 0;          // 批次序号: 防旧批次迟到信号污染新批次

    void finishBatch();                 // 全部完成后排序并发出 resultReady
    bool cleanupJob(ActiveJob &job, int waitTimeoutMs);

    // 孤儿线程回收: cleanupAll 超时(极端情况)放弃的线程不永久泄漏——
    // 移入 m_orphans 后由后台定时器周期性再试 wait, 退出后 deleteLater。
    // 进程存活期间最终全部回收; 析构时再兜底一次(超时仍不退才解除父子关系)
    QVector<ActiveJob> m_orphans;
    QTimer *m_reapTimer = nullptr;
    void startReapTimer();
    void reapOrphans();
};

#endif // BUSINESS_PASSPREDICTORSERVICE_H
