// =============================================================================
// PassPredictorService.cpp — 预报调度实现（修复版：正确的线程生命周期管理）
// =============================================================================
#include "PassPredictorService.h"
#include <QThread>
#include <algorithm>

PassPredictorService::PassPredictorService(QObject *parent)
    : QObject(parent) {}

PassPredictorService::~PassPredictorService()
{
    // 析构路径: 带超时等待，防止窗口关闭时被长时间批量任务卡死。
    // 协作取消使任务通常毫秒级退出；超时(极端情况)后线程在后台自然后退，
    // 进程退出时由系统回收，绝不在主线程无限阻塞。
    cleanupAll(2000);
}

void PassPredictorService::startJob(const PassPredictWorker::Job &job)
{
    cleanupAll(2000);

    // 单星任务不走批量汇总; 递增序号使在途批量任务的迟到信号全部失效
    // （cleanupAll→cancelAll 已递增一次，此处唯一确定本任务取到的序号）
    const int seq = ++m_batchSeq;

    auto *thread = new QThread(this);
    auto *worker = new PassPredictWorker;
    worker->moveToThread(thread);

    // resultReady → quit thread
    connect(worker, &PassPredictWorker::resultReady, thread, &QThread::quit);
    // errorOccurred → also quit thread（修复：之前不 quit 导致线程泄漏）
    connect(worker, &PassPredictWorker::errorOccurred, thread, &QThread::quit);
    // 线程结束 → 清理 worker
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    // 转发信号（metaInfo 同样带序号守卫: 取消/换任务后迟到元信息一律丢弃,
    // 防止状态栏显示已淘汰任务的模型与历元）
    connect(worker, &PassPredictWorker::resultReady, this,
            [this, seq](QVector<PassEvent> passes) {
                if (seq != m_batchSeq) return;
                emit resultReady(passes);
            });
    connect(worker, &PassPredictWorker::errorOccurred, this,
            [this, seq](QString msg) {
                if (seq != m_batchSeq) return;
                emit errorOccurred(msg);
            });
    connect(worker, &PassPredictWorker::metaInfo, this,
            [this, seq](QString model, QString epochUtc, double ageDays) {
                if (seq != m_batchSeq) return;
                emit metaInfo(model, epochUtc, ageDays);
            });

    m_activeJobs.append({thread, worker});

    // 启动线程（在线程 started 信号中调用 worker->run）
    connect(thread, &QThread::started, worker, [worker, job]() {
        worker->run(job);
    });
    // QThread::start 返回 void: 线程创建失败时 Qt 内部致命退出，不会静默失败
    thread->start();
}

void PassPredictorService::startBatch(const QVector<PassPredictWorker::Job> &jobs)
{
    cleanupAll(2000);

    if (jobs.isEmpty()) {
        emit errorOccurred("无有效 TLE");
        return;
    }

    m_batchPasses.clear();
    m_batchCompleted = 0;
    m_batchTotal = jobs.size();
    m_batchFinished = false;   // 新批次开始, 允许转发单星元信息
    const int seq = ++m_batchSeq;   // 本批次序号: 迟到的旧批次信号在此校验丢弃

    // 固定大小 worker 池（H2 修复）: 逐颗建线程在 starlink/active 等大组
    // （数千颗）下会耗尽线程资源并静默挂死。每个 worker 顺序消费一个切片。
    const int maxWorkers   = qBound(1, QThread::idealThreadCount() * 2, 16);
    const int workerCount  = qMin(maxWorkers, m_batchTotal);

    for (int w = 0; w < workerCount; ++w) {
        // 任务轮转均分: worker w 处理下标 w, w+workerCount, ... 的任务
        QVector<PassPredictWorker::Job> slice;
        for (int i = w; i < m_batchTotal; i += workerCount)
            slice.append(jobs[i]);

        auto *thread = new QThread(this);
        auto *worker = new PassPredictWorker;
        worker->moveToThread(thread);

        // 成功: 汇总结果并计数
        connect(worker, &PassPredictWorker::resultReady, this,
                [this, seq](QVector<PassEvent> passes) {
                    if (seq != m_batchSeq) return;   // 旧批次迟到信号: 丢弃
                    m_batchPasses.append(passes);
                    m_batchCompleted++;
                    emit batchProgress(m_batchCompleted, m_batchTotal);
                    if (m_batchCompleted == m_batchTotal) finishBatch();
                });

        // 失败: 转发错误并计数（否则批量永远等不到完成 → 悬挂）
        connect(worker, &PassPredictWorker::errorOccurred, this,
                [this, seq](QString msg) {
                    if (seq != m_batchSeq) return;   // 旧批次迟到信号: 丢弃
                    m_batchCompleted++;
                    emit batchProgress(m_batchCompleted, m_batchTotal);
                    emit errorOccurred(msg);
                    if (m_batchCompleted == m_batchTotal) finishBatch();
                });

        connect(worker, &PassPredictWorker::resultReady, thread, &QThread::quit);
        connect(worker, &PassPredictWorker::errorOccurred, thread, &QThread::quit);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &PassPredictWorker::metaInfo, this,
                [this, seq](QString model, QString epochUtc, double ageDays) {
                    // 批次收尾(resultReady 已发出)后, 同批次内迟到 worker 的
                    // 单星元信息不再转发——否则会覆盖"批量 N 颗"汇总并随机
                    // 改写历元栏（队列投递顺序无保证）
                    if (seq != m_batchSeq || m_batchFinished) return;
                    emit metaInfo(model, epochUtc, ageDays);
                });

        m_activeJobs.append({thread, worker});

        connect(thread, &QThread::started, worker, [worker, slice]() {
            worker->runJobs(slice);
        });
        // QThread::start 返回 void: 线程创建失败时 Qt 内部致命退出，不会静默失败
        thread->start();
    }
}

void PassPredictorService::finishBatch()
{
    // 按可见性优先 + 最大仰角降序排列
    std::sort(m_batchPasses.begin(), m_batchPasses.end(),
        [](const PassEvent &a, const PassEvent &b) {
            if (a.visibility != b.visibility)
                return static_cast<int>(a.visibility) < static_cast<int>(b.visibility);
            return a.maxElevDeg > b.maxElevDeg;
        });
    emit resultReady(m_batchPasses);
    m_batchFinished = true;   // 收尾: 迟到的单星 metaInfo 不再转发
    m_batchPasses.clear();
}

void PassPredictorService::cancelAll()
{
    // 递增批次序号: 取消后本批次迟到的结果/错误信号一律丢弃，
    // 杜绝"取消后结果又冒出来"的竞态（M5）
    ++m_batchSeq;
    for (auto &aj : m_activeJobs) {
        if (aj.worker) {
            aj.worker->cancel();   // 协作式取消，线程会快速自然退出
        }
    }
}

bool PassPredictorService::cleanupJob(ActiveJob &aj, int waitTimeoutMs)
{
    if (!aj.thread) {
        aj.worker = nullptr;
        return true;
    }
    if (aj.thread->isRunning()) {
        // 请求事件循环退出并等待。协作式取消保证线程快速退出，
        // 不再使用已弃用的 QThread::terminate()
        aj.thread->quit();
        bool exited = (waitTimeoutMs > 0)
            ? aj.thread->wait(waitTimeoutMs)
            : aj.thread->wait();
        if (!exited) {
            // 超时: 线程仍在运行（极端情况）。放弃等待，让线程在后台
            // 自然后退。解除父子关系防止析构时 delete 正在运行的
            // QThread（未定义行为）；不再 deleteLater，对象随进程退出回收。
            aj.thread->setParent(nullptr);
            aj.thread = nullptr;
            aj.worker = nullptr;
            return false;
        }
    } else {
        // 线程未被观察为运行状态（尚未启动或已结束）: 防御性地 quit()
        // 一次——若 start() 已发出而 OS 线程即将进入运行, quit 会让其
        // 立即退出, 避免 deleteLater 落在"正在启动/运行"的线程上
        aj.thread->quit();
    }
    aj.thread->deleteLater();
    aj.thread = nullptr;
    aj.worker = nullptr;  // worker 已通过 finished → deleteLater 被清理
    return true;
}

void PassPredictorService::cleanupAll(int waitTimeoutMs)
{
    // 先取消，给线程时间自然退出
    cancelAll();
    // 全局截止时间（M7）: 避免逐线程串行等待在任务多时总时长 N×timeout。
    // 所有线程共享同一剩余时间预算，超时后后续线程仅试探性等待（立即返回）。
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + waitTimeoutMs;
    for (auto &aj : m_activeJobs) {
        int remaining = waitTimeoutMs;
        if (waitTimeoutMs > 0) {
            remaining = static_cast<int>(
                deadline - QDateTime::currentMSecsSinceEpoch());
            if (remaining <= 0)
                remaining = 0;   // 已超时: 仅试探性等待（wait(0) 立即返回）
        }
        cleanupJob(aj, remaining);
    }
    m_activeJobs.clear();
}
