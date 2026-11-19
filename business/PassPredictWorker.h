#ifndef BUSINESS_PASSPREDICTWORKER_H
#define BUSINESS_PASSPREDICTWORKER_H
// =============================================================================
// PassPredictWorker.h — 工作在独立 QThread 中的过境预报执行器
// =============================================================================
#include <QObject>
#include <QVector>
#include <QDateTime>
#include <atomic>
#include "core/DataTypes.h"
#include "core/OrbitCalculator.h"
#include "core/AtomicCancelFlag.h"

// 注册跨线程信号槽所需的自定义类型
Q_DECLARE_METATYPE(QVector<PassEvent>)
// PassEvent 自身也注册: 供 QVariant 直接存取（回环/调试/序列化扩展）
Q_DECLARE_METATYPE(PassEvent)

class PassPredictWorker : public QObject {
    Q_OBJECT
public:
    struct Job {
        QString tleLine0;          // 卫星名称行（可选）
        QString tleLine1;          // TLE 第一行
        QString tleLine2;          // TLE 第二行
        double  observerLatDeg  = 33.0;
        double  observerLonDeg  = 103.0;
        double  observerAltKm   = 0.05;
        double  minElevDeg      = 10.0;
        QDateTime startUtc;        // 预报起始 UTC 时间
        double  hoursAhead       = 24.0;
        OrbitCalculator::Config calcCfg;
    };

    explicit PassPredictWorker(QObject *parent = nullptr);
    ~PassPredictWorker() override = default;

public slots:
    void run(Job job);
    // 批量切片: 依次执行多个任务（线程池模式，固定 worker 数消费全部任务）
    void runJobs(const QVector<Job> &jobs);
    void cancel();

signals:
    void resultReady(QVector<PassEvent> passes);
    void errorOccurred(QString message);
    // TLE 元信息: 传播模型 / 历元(UTC 字符串) / 数据龄期(天)
    void metaInfo(QString model, QString epochUtc, double ageDays);
    // 任务被自动恢复策略跳过:
    // 不 emit errorOccurred, 但必须通知调度层——否则单星 UI 永久"计算中"、
    // 批量 completed 计数缺失导致 resultReady 永不发出（悬挂）
    void jobSkipped(QString satelliteId, QString reason);

private:
    AtomicCancelFlagPtr m_cancelFlag;
    void runOneJob(const Job &job);   // 单任务执行体（run/runJobs 共用）
};

#endif // BUSINESS_PASSPREDICTWORKER_H
