// =============================================================================
// tst_passworker.cpp — PassPredictWorker 业务逻辑单元测试
// runJobs/run 是普通槽: 直接同步调用验证结果/错误/取消三条路径
// 真实线程调度与信号队列投递由 tst_passservice 覆盖
// =============================================================================
#include <QtTest/QtTest>
#include "business/PassPredictWorker.h"
#include "core/ErrorHandler.h"
#include <QSignalSpy>
#include <QDate>
#include <QTimeZone>
#include <atomic>

namespace {

// 有效 TLE: ISS (ZARYA), 2022-03-12 历元（校验和有效, 与 tst_propagator 同源）
const char *ISS_L1 = "1 25544U 98067A   22071.78032407  .00021395  00000-0  39008-3 0  9996";
const char *ISS_L2 = "2 25544  51.6424  94.0370 0004047 256.5103  89.8846 15.49386383330227";

// 无效 TLE: 行1 行号前缀 9 → BAD_LINE_NUMBER（ERROR 级, 恢复失败 → errorOccurred）
const char *BAD_L1 = "9 25544U 98067A   22071.78032407  .00021395  00000-0  39008-3 0  9996";

// 恢复策略是 ErrorHandler 的可扩展公共契约（registerRecoveryFunction）。
// 本套件用受控原子开关替换 TLE_PARSE_ERROR 恢复函数, 构造"恢复成功"场景,
// 验证 worker 必须走 jobSkipped 而非静默（否则批量悬挂/单星卡死）。
// 注意: WARNING 级校验和错误在 TleParser 是"不中断仅记录"(parse 成功出结果),
// 不经过恢复路径——不能用它触发 jobSkipped。
static std::atomic<bool> g_forceRecover{false};

PassPredictWorker::Job makeJob(const QString &l1, const QString &l2)
{
    PassPredictWorker::Job job;
    job.tleLine0 = "TEST-SAT";
    job.tleLine1 = l1;
    job.tleLine2 = l2;
    job.observerLatDeg = 39.9042;
    job.observerLonDeg = 116.4074;
    job.observerAltKm = 0.05;
    job.minElevDeg = 10.0;
    // 预报窗口贴近 TLE 历元（2022-03-12）: SGP4 在远离历元处精度退化,
    // 4 年以上陈旧根数可能完全失效（0 过境）——窗口必须与历元同年代
    job.startUtc = QDateTime(QDate(2022, 3, 12), QTime(19, 0, 0), QTimeZone::UTC);
    job.hoursAhead = 24.0;
    return job;
}

} // namespace

class TstPassWorker : public QObject {
    Q_OBJECT
private slots:
    void validTleProducesPasses();
    void invalidTleEmitsError();
    void preCancelledSkipsEverything();
    void cancelledMidwayStopsRemainingJobs();
    void recoveredTleEmitsJobSkipped();
};

void TstPassWorker::validTleProducesPasses()
{
    PassPredictWorker worker;
    QSignalSpy ready(&worker, &PassPredictWorker::resultReady);
    QSignalSpy err(&worker, &PassPredictWorker::errorOccurred);

    worker.run(makeJob(ISS_L1, ISS_L2));

    QCOMPARE(ready.count(), 1);
    QCOMPARE(err.count(), 0);
    QVector<PassEvent> passes = ready.at(0).at(0).value<QVector<PassEvent>>();
    QVERIFY2(!passes.isEmpty(), "24h 窗口 ISS 应有至少一次过境");
}

void TstPassWorker::invalidTleEmitsError()
{
    PassPredictWorker worker;
    QSignalSpy ready(&worker, &PassPredictWorker::resultReady);
    QSignalSpy err(&worker, &PassPredictWorker::errorOccurred);

    worker.run(makeJob(BAD_L1, ISS_L2));

    QCOMPARE(ready.count(), 0);
    QCOMPARE(err.count(), 1);
    QVERIFY(err.at(0).at(0).toString().contains("TLE"));
}

void TstPassWorker::preCancelledSkipsEverything()
{
    PassPredictWorker worker;
    worker.cancel();   // 启动前已取消: runJobs 首轮循环即跳出

    QSignalSpy ready(&worker, &PassPredictWorker::resultReady);
    QSignalSpy err(&worker, &PassPredictWorker::errorOccurred);

    worker.run(makeJob(ISS_L1, ISS_L2));

    QCOMPARE(ready.count(), 0);
    QCOMPARE(err.count(), 0);
}

void TstPassWorker::cancelledMidwayStopsRemainingJobs()
{
    // 切片 2 个任务: 第 1 个完成后置取消, 第 2 个不得执行（无 resultReady）
    PassPredictWorker worker;
    QSignalSpy ready(&worker, &PassPredictWorker::resultReady);
    QSignalSpy err(&worker, &PassPredictWorker::errorOccurred);

    QVector<PassPredictWorker::Job> jobs;
    jobs << makeJob(ISS_L1, ISS_L2);

    worker.runJobs(jobs);
    worker.cancel();
    jobs << makeJob(ISS_L1, ISS_L2);
    worker.runJobs(jobs);

    // 第 1 批完成 → 1 次 resultReady; 第 2 批被取消 → 无新增信号
    QCOMPARE(ready.count(), 1);
    QCOMPARE(err.count(), 0);
}

void TstPassWorker::recoveredTleEmitsJobSkipped()
{
    // 构造"恢复成功"场景: 解析失败但 ErrorHandler 判定自动恢复 → 必须走
    // jobSkipped（不得静默——批量 completed 计数靠信号驱动, 缺失即悬挂）
    ErrorHandler &h = ErrorHandler::instance();
    h.registerRecoveryFunction(ErrorType::TLE_PARSE_ERROR,
        [](const ErrorInfo &) { return g_forceRecover.load(); });
    g_forceRecover = true;

    PassPredictWorker worker;
    QSignalSpy ready(&worker, &PassPredictWorker::resultReady);
    QSignalSpy err(&worker, &PassPredictWorker::errorOccurred);
    QSignalSpy skipped(&worker, &PassPredictWorker::jobSkipped);

    worker.run(makeJob(BAD_L1, ISS_L2));

    g_forceRecover = false;   // 断言前复位: 断言失败也不污染后续套件
    QCOMPARE(ready.count(), 0);
    QCOMPARE(err.count(), 0);
    QCOMPARE(skipped.count(), 1);
}

int runTstPassWorker(int argc, char *argv[])
{
    TstPassWorker t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_passworker.moc"