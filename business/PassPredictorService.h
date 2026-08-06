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

signals:
    void resultReady(QVector<PassEvent> passes);
    void progressUpdate(int percent);
    void errorOccurred(QString message);
    void batchProgress(int completed, int total);

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

    void finishBatch();                 // 全部完成后排序并发出 resultReady
    void cleanupJob(ActiveJob &job);
    void cleanupAll();
};

#endif // BUSINESS_PASSPREDICTORSERVICE_H
