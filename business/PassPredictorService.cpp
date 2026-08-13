// =============================================================================
// PassPredictorService.cpp — 预报调度实现（修复版：正确的线程生命周期管理）
// =============================================================================
#include "PassPredictorService.h"
#include <QThread>
#include <QTimer>
#include <algorithm>

PassPredictorService::PassPredictorService(QObject *parent)
    : QObject(parent) {}

PassPredictorService::~PassPredictorService()
{
    // 析构路径: 带超时等待，防止窗口关闭时被长时间批量任务卡死。
    // 协作取消使任务通常毫秒级退出；超时(极端情况)后线程在后台自然后退，
    // 进程退出时由系统回收，绝不在主线程无限阻塞。
    cleanupAll(2000);

    // 孤儿线程兜底: 析构子对象前必须回收或解除父子关系，
    // 否则 QThread 仍在运行时析构会触发未定义行为
    if (m_reapTimer) m_reapTimer->stop();
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 2000;
    for (auto &aj : m_orphans) {
        QThread *t = aj.thread;
        if (!t) continue;
        t->quit();
        int remaining = static_cast<int>(deadline - QDateTime::currentMSecsSinceEpoch());
        if (!t->wait(std::max(0, remaining))) {
            t->setParent(nullptr);   // 仍未退出: 解除父子关系，随进程退出回收
            aj.worker = nullptr;
        } else {
            t->deleteLater();
        }
    }
    m_orphans.clear();
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
    // 预分配: PassEvent 含 std::vector<PassSample>, QVector 扩容重分配会对
    // 存量元素整体深拷贝(非共享), 千颗级批量下是 O(n²) 开销
    // 16 倍: 低轨 168h 窗口单星可达 100+ 过境, 4 倍预估偏小
    m_batchPasses.reserve(qMin<qsizetype>(m_batchTotal * 16, 400000));
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
                    // std::move: PassEvent 含采样向量, 拷贝即整段深拷贝,
                    // 千颗级大组下这是纯开销
                    m_batchPasses.append(std::move(passes));
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
    // emit 前先移动到临时量: 信号参数按值接收, 直接 emit 成员会深拷贝
    // 整批采样向量; 移动后主线程交付只剩队列转发那一份必要拷贝
    QVector<PassEvent> out = std::move(m_batchPasses);
    // 置位必须早于 emit: resultReady 在主线程同步触发 onResultsReady,
    // 其内部模态错误弹窗会开嵌套事件循环——若此时 m_batchFinished 仍为
    // false, 队列中迟到的单星 metaInfo 会通过守卫并被 onMetaInfo 采用,
    // 用随机一颗卫星的模型/历元覆盖批量汇总文本
    m_batchFinished = true;   // 收尾: 迟到的单星 metaInfo 不再转发
    emit resultReady(std::move(out));
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
            // 超时: 线程仍在运行（极端情况）。不等了——
            // 移入孤儿列表交给后台定时器二次回收（进程存活期间最终
            // 全部回收成功），而不是永久泄漏或即使执行 delete 正在运行
            // 的 QThread（未定义行为）。
            m_orphans.append({aj.thread, aj.worker});
            startReapTimer();
            return false;
        }
    } else {
        // 线程未被观察为运行状态: 若已自然结束（isFinished），finished 信号
        // 在连接建立前就已发射，finished→deleteLater 永不触发——必须直接
        // deleteLater，否则 QThread 对象随会话累积（每完成一次单星任务
        // 就残留一个子对象直到 service 析构）
        if (aj.thread->isFinished()) {
            aj.thread->deleteLater();
            return true;
        }
        // 尚未启动（理论上不可达: start() 紧跟构造）: 防御性 quit()。
        // 删除改由 finished→deleteLater 兜底——若 OS 线程尚未进入 run(),
        // 主循环当轮处理 DeferredDelete 时线程可能仍在启动, 直接
        // deleteLater 会触发 "QThread: Destroyed while still running"
        aj.thread->quit();
        QObject::connect(aj.thread, &QThread::finished,
                         aj.thread, &QObject::deleteLater);
        return true;
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

void PassPredictorService::startReapTimer()
{
    if (m_reapTimer) return;   // 已在运行
    m_reapTimer = new QTimer(this);
    m_reapTimer->setInterval(1000);
    connect(m_reapTimer, &QTimer::timeout, this, &PassPredictorService::reapOrphans);
    m_reapTimer->start();
}

void PassPredictorService::reapOrphans()
{
    for (int i = m_orphans.size() - 1; i >= 0; --i) {
        ActiveJob &aj = m_orphans[i];
        QThread *t = aj.thread;
        if (!t || !t->isRunning()) {
            // 已自然退出（或已回收）: 补 deleteLater（finished→worker 删除
            // 已由线程内连接完成），线程对象由主线程事件循环删除
            if (t) t->deleteLater();
            aj.thread = nullptr;
            aj.worker = nullptr;
            m_orphans.removeAt(i);
            continue;
        }
        t->quit();
        if (t->wait(500)) {          // 单片限时等待: 退出则回收
            t->deleteLater();
            aj.thread = nullptr;
            aj.worker = nullptr;
            m_orphans.removeAt(i);
        }
        // 仍未退出 → 保留待下一轮
    }
    if (m_orphans.isEmpty() && m_reapTimer) {
        m_reapTimer->stop();         // 全部回收完毕 → 停表省电
        m_reapTimer->deleteLater();
        m_reapTimer = nullptr;
    }
}
