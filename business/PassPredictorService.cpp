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
    cleanupAll();

    // 单星任务不走批量汇总; 递增序号使在途批量任务的迟到信号全部失效
    ++m_batchSeq;

    auto *thread = new QThread(this);
    auto *worker = new PassPredictWorker;
    worker->moveToThread(thread);

    // resultReady → quit thread
    connect(worker, &PassPredictWorker::resultReady, thread, &QThread::quit);
    // errorOccurred → also quit thread（修复：之前不 quit 导致线程泄漏）
    connect(worker, &PassPredictWorker::errorOccurred, thread, &QThread::quit);
    // 线程结束 → 清理 worker
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    // 转发信号
    connect(worker, &PassPredictWorker::resultReady,
            this, &PassPredictorService::resultReady);
    connect(worker, &PassPredictWorker::errorOccurred,
            this, &PassPredictorService::errorOccurred);

    m_activeJobs.append({thread, worker});

    // 启动线程（在线程 started 信号中调用 worker->run）
    connect(thread, &QThread::started, worker, [worker, job]() {
        worker->run(job);
    });
    thread->start();
}

void PassPredictorService::startBatch(const QVector<PassPredictWorker::Job> &jobs)
{
    cleanupAll();

    if (jobs.isEmpty()) {
        emit errorOccurred("无有效 TLE");
        return;
    }

    m_batchPasses.clear();
    m_batchCompleted = 0;
    m_batchTotal = jobs.size();
    const int seq = ++m_batchSeq;   // 本批次序号: 迟到的旧批次信号在此校验丢弃

    for (int i = 0; i < m_batchTotal; ++i) {
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

        m_activeJobs.append({thread, worker});

        connect(thread, &QThread::started, worker, [worker, job = jobs[i]]() {
            worker->run(job);
        });
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
    m_batchPasses.clear();
}

void PassPredictorService::cancelAll()
{
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
            // 自然后退；不 deleteLater（running 中析构会告警），
            // 对象随进程退出回收。
            aj.thread = nullptr;
            aj.worker = nullptr;
            return false;
        }
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
    // 清理所有 job
    for (auto &aj : m_activeJobs) {
        cleanupJob(aj, waitTimeoutMs);
    }
    m_activeJobs.clear();
}
