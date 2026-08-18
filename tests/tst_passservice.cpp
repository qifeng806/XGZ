// =============================================================================
// tst_passservice.cpp — PassPredictorService 并发调度集成测试
// 真实 QThread 线程池 + 队列信号投递: 覆盖批量汇总/错误转发/进度/取消/单星
// =============================================================================
#include <QtTest/QtTest>
#include "business/PassPredictorService.h"
#include "core/ErrorHandler.h"
#include <QSignalSpy>
#include <QDate>
#include <QTimeZone>
#include <atomic>

namespace {

// 有效 TLE: ISS (ZARYA), 与 tst_passworker 同源
const char *ISS_L1 = "1 25544U 98067A   22071.78032407  .00021395  00000-0  39008-3 0  9996";
const char *ISS_L2 = "2 25544  51.6424  94.0370 0004047 256.5103  89.8846 15.49386383330227";

// 无效 TLE: 行1 行号前缀 9 → BAD_LINE_NUMBER（ERROR 级, 恢复失败 → errorOccurred）
const char *BAD_L1 = "9 25544U 98067A   22071.78032407  .00021395  00000-0  39008-3 0  9996";

// 受控替换 TLE_PARSE_ERROR 恢复函数, 构造"恢复成功"场景（真实 WARNING 级
// 校验和错误在 TleParser 不阻断, parse 成功出结果, 到不了恢复路径）
static std::atomic<bool> g_forceRecover{false};

PassPredictWorker::Job makeJob(const QString &name, const QString &l1, const QString &l2,
                               double hoursAhead = 24.0)
{
    PassPredictWorker::Job job;
    job.tleLine0 = name;
    job.tleLine1 = l1;
    job.tleLine2 = l2;
    job.observerLatDeg = 39.9042;
    job.observerLonDeg = 116.4074;
    job.observerAltKm = 0.05;
    job.minElevDeg = 10.0;
    // 预报窗口贴近 TLE 历元（2022-03-12）: SGP4 在远离历元处精度退化,
    // 4 年以上陈旧根数可能完全失效（0 过境）——窗口必须与历元同年代
    job.startUtc = QDateTime(QDate(2022, 3, 12), QTime(19, 0, 0), QTimeZone::UTC);
    job.hoursAhead = hoursAhead;
    return job;
}

} // namespace

class TstPassService : public QObject {
    Q_OBJECT
private slots:
    void batchMixedResults();
    void emptyBatchEmitsError();
    void singleJobWorks();
    void cancelAllSuppressesLateResults();
    void batchWithRecoveredTleStillCompletes();
    void singleJobWithRecoveredTleReportsSkip();
};

void TstPassService::batchMixedResults()
{
    PassPredictorService svc;
    QSignalSpy ready(&svc, &PassPredictorService::resultReady);
    QSignalSpy err(&svc, &PassPredictorService::errorOccurred);
    QSignalSpy prog(&svc, &PassPredictorService::batchProgress);

    QVector<PassPredictWorker::Job> jobs;
    jobs << makeJob("ISS-1", ISS_L1, ISS_L2);
    jobs << makeJob("ISS-2", ISS_L1, ISS_L2);
    jobs << makeJob("BAD-1", BAD_L1, ISS_L2);
    jobs << makeJob("BAD-2", BAD_L1, ISS_L2);
    svc.startBatch(jobs);

    QVERIFY2(ready.wait(30000), "批量应在 30s 内完成");

    // resultReady 恰好在最后一个完成计数内发出 → 此时全部错误已转发完毕
    QCOMPARE(ready.count(), 1);
    QCOMPARE(err.count(), 2);
    for (const auto &args : err) {
        QVERIFY(args.at(0).toString().contains("TLE"));
    }

    // 进度单调递增, 终值 == 总数
    int last = -1;
    for (const auto &args : prog) {
        const int completed = args.at(0).toInt();
        const int total = args.at(1).toInt();
        QCOMPARE(total, 4);
        QVERIFY2(completed > last, "batchProgress 必须严格单调");
        last = completed;
    }
    QCOMPARE(last, 4);

    // 结果非空（2 颗有效卫星 24h 至少各 1 次过境, 且按可见性排序汇总）
    QVector<PassEvent> passes = ready.at(0).at(0).value<QVector<PassEvent>>();
    QVERIFY(!passes.isEmpty());
}

void TstPassService::emptyBatchEmitsError()
{
    PassPredictorService svc;
    QSignalSpy err(&svc, &PassPredictorService::errorOccurred);
    QSignalSpy ready(&svc, &PassPredictorService::resultReady);

    // startBatch 空任务: 同步 emit, 无需 wait（QSignalSpy::wait 只等"期间新到"信号）
    svc.startBatch({});

    QCOMPARE(err.count(), 1);
    QVERIFY(err.at(0).at(0).toString().contains("无有效 TLE"));
    QCOMPARE(ready.count(), 0);
}

void TstPassService::singleJobWorks()
{
    PassPredictorService svc;
    QSignalSpy ready(&svc, &PassPredictorService::resultReady);
    QSignalSpy err(&svc, &PassPredictorService::errorOccurred);

    svc.startJob(makeJob("ISS", ISS_L1, ISS_L2));

    QVERIFY2(ready.wait(30000), "单星任务应在 30s 内完成");
    QCOMPARE(ready.count(), 1);
    QCOMPARE(err.count(), 0);
    QVERIFY(!ready.at(0).at(0).value<QVector<PassEvent>>().isEmpty());
}

void TstPassService::cancelAllSuppressesLateResults()
{
    PassPredictorService svc;
    QSignalSpy ready(&svc, &PassPredictorService::resultReady);
    QSignalSpy err(&svc, &PassPredictorService::errorOccurred);

    // 8 × 168h 长窗口: 保证取消时刻所有 worker 仍在计算
    QVector<PassPredictWorker::Job> jobs;
    for (int i = 0; i < 8; ++i)
        jobs << makeJob(QString("LONG-%1").arg(i), ISS_L1, ISS_L2, 168.0);
    svc.startBatch(jobs);

    QTest::qWait(100);   // 等待线程启动并进入计算
    svc.cancelAll();

    // 协作取消后不得再有结果/错误信号（迟到信号被序号守卫丢弃）
    QVERIFY(!ready.wait(1500));
    QCOMPARE(err.count(), 0);

    // 服务析构 cleanupAll(2000) 兜底回收线程: 不悬挂即通过
}

void TstPassService::batchWithRecoveredTleStillCompletes()
{
    // 恢复成功（跳过）的任务必须计数: 否则 completed 永远 < total,
    // resultReady 永不发出 → 批量悬挂（回归保护）
    ErrorHandler &h = ErrorHandler::instance();
    h.registerRecoveryFunction(ErrorType::TLE_PARSE_ERROR,
        [](const ErrorInfo &) { return g_forceRecover.load(); });
    g_forceRecover = true;

    PassPredictorService svc;
    QSignalSpy ready(&svc, &PassPredictorService::resultReady);
    QSignalSpy err(&svc, &PassPredictorService::errorOccurred);
    QSignalSpy prog(&svc, &PassPredictorService::batchProgress);

    // 1 有效 + 1 被跳过: 跳过以"已跳过"错误消息形态进入批量错误汇总
    QVector<PassPredictWorker::Job> jobs;
    jobs << makeJob("ISS-1", ISS_L1, ISS_L2);
    jobs << makeJob("BAD-1", BAD_L1, ISS_L2);
    svc.startBatch(jobs);

    QVERIFY2(ready.wait(30000), "含恢复成功任务的批量也应在 30s 内完成（防悬挂）");
    g_forceRecover = false;   // 断言前复位: 断言失败也不污染后续套件

    QCOMPARE(ready.count(), 1);
    QCOMPARE(err.count(), 1);
    QVERIFY(err.at(0).at(0).toString().contains("已跳过"));

    int last = -1;
    for (const auto &args : prog) {
        QCOMPARE(args.at(1).toInt(), 2);
        QVERIFY2(args.at(0).toInt() > last, "batchProgress 必须严格单调");
        last = args.at(0).toInt();
    }
    QCOMPARE(last, 2);

    // 结果仅含有效卫星的过境
    QVector<PassEvent> passes = ready.at(0).at(0).value<QVector<PassEvent>>();
    QVERIFY(!passes.isEmpty());
}

void TstPassService::singleJobWithRecoveredTleReportsSkip()
{
    ErrorHandler &h = ErrorHandler::instance();
    h.registerRecoveryFunction(ErrorType::TLE_PARSE_ERROR,
        [](const ErrorInfo &) { return g_forceRecover.load(); });
    g_forceRecover = true;

    PassPredictorService svc;
    QSignalSpy ready(&svc, &PassPredictorService::resultReady);
    QSignalSpy err(&svc, &PassPredictorService::errorOccurred);

    // 单星模式: jobSkipped 必须以错误消息形态送达 UI（否则"计算中"状态永不结束）
    svc.startJob(makeJob("BAD-1", BAD_L1, ISS_L2));

    QVERIFY2(err.wait(30000), "单星恢复成功任务应通过错误通道报告跳过");
    g_forceRecover = false;

    QCOMPARE(err.count(), 1);
    QVERIFY(err.at(0).at(0).toString().contains("已跳过"));
    QCOMPARE(ready.count(), 0);
}

int runTstPassService(int argc, char *argv[])
{
    TstPassService t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_passservice.moc"