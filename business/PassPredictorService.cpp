// =============================================================================
// PassPredictorService.cpp — 预报调度实现（修复版：正确的线程生命周期管理）
// =============================================================================
#include "PassPredictorService.h"
#include <QThread>
#include <QTimer>
#include <algorithm>

PassPredictorService::PassPredictorService(QObject *parent)
    : QObject(parent), m_batchCompleted(0), m_batchSeq(0), m_batchFinished(false) {}

PassPredictorService::~PassPredictorService()
{
    // 析构路径: 带超时等待，防止窗口关闭时被长时间批量任务卡死。
    // 协作取消使任务通常毫秒级退出；超时(极端情况)后线程在后台自然后退，
    // 进程退出时由系统回收，绝不在主线程无限阻塞。
    cleanupAll(2000);

    // 孤儿线程兜底: 析构子对象前必须回收或解除父子关系，
    // 否则 QThread 仍在运行时析构会触发未定义行为
    m_reapTimer.reset();
    
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 2000;
    for (auto &aj : m_orphans) {
        QThread *t = aj.thread;
        if (!t) continue;
        
        // 先尝试协作退出
        t->quit();
        int remaining = static_cast<int>(deadline - QDateTime::currentMSecsSinceEpoch());
        if (remaining > 0 && t->wait(remaining)) {
            // 正常退出，安全删除
            t->deleteLater();
        } else {
            // 超时或未响应：强制解除父子关系，避免析构时的未定义行为
            t->setParent(nullptr);
        // worker 由 shared_ptr 自动管理，无需手动删除
        aj.worker.reset();
        }
        aj.thread = nullptr;
    }
    m_orphans.clear();
}

void PassPredictorService::startJob(const PassPredictWorker::Job &job)
{
    cleanupAll(2000);

    // 单星任务不走批量汇总; 递增序号使在途批量任务的迟到信号全部失效
    // （cleanupAll→cancelAll 已递增一次，此处唯一确定本任务取到的序号）
    const int seq = ++m_batchSeq;    auto thread = std::make_unique<QThread>(this);
    auto worker = std::make_shared<PassPredictWorker>();
    worker->moveToThread(thread.get());

    // resultReady → quit thread
    connect(worker.get(), &PassPredictWorker::resultReady, thread.get(), &QThread::quit);
    // errorOccurred → also quit thread（修复：之前不 quit 导致线程泄漏）
    connect(worker.get(), &PassPredictWorker::errorOccurred, thread.get(), &QThread::quit);
    // 线程结束 → 清理 worker (由 shared_ptr 自动管理)
    connect(thread.get(), &QThread::finished, worker.get(), &QObject::deleteLater);
    // 转发信号（metaInfo 同样带序号守卫: 取消/换任务后迟到元信息一律丢弃,
    // 防止状态栏显示已淘汰任务的模型与历元）
    connect(worker.get(), &PassPredictWorker::resultReady, this,
            [this, seq](QVector<PassEvent> passes) {
                if (seq != m_batchSeq) return;
                emit resultReady(passes);
            });
    connect(worker.get(), &PassPredictWorker::errorOccurred, this,
            [this, seq](QString msg) {
                if (seq != m_batchSeq) return;
                emit errorOccurred(msg);
            });
    connect(worker.get(), &PassPredictWorker::metaInfo, this,
            [this, seq](QString model, QString epochUtc, double ageDays) {
                if (seq != m_batchSeq) return;
                emit metaInfo(model, epochUtc, ageDays);
            });

    m_activeJobs.append({thread.get(), worker, QDateTime::currentMSecsSinceEpoch()});

    // 启动线程（在线程 started 信号中调用 worker->run）
    connect(thread.get(), &QThread::started, worker.get(), [worker, job]() {
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

    // 本批次序号: 迟到的旧批次信号在此校验丢弃（worker 池各 lambda 共用）
    const int seq = ++m_batchSeq;

    {
        QMutexLocker locker(&m_batchMutex);
        m_batchPasses.clear();
        m_batchCompleted = 0;
        m_batchTotal = jobs.size();
        m_batchFinished = false;   // 新批次开始, 允许转发单星元信息
        // 预分配: PassEvent 含 std::vector<PassSample>, QVector 扩容重分配会对
        // 存量元素整体深拷贝(非共享), 千颗级批量下是 O(n²) 开销
        // 16 倍: 低轨 168h 窗口单星可达 100+ 过境, 4 倍预估偏小
        m_batchPasses.reserve(qMin<qsizetype>(m_batchTotal * 16, 400000));
    }

    // 固定大小 worker 池（H2 修复）: 逐颗建线程在 starlink/active 等大组
    // （数千颗）下会耗尽线程资源并静默挂死。每个 worker 顺序消费一个切片。
    const int maxWorkers   = qBound(1, QThread::idealThreadCount() * 2, 16);
    const int workerCount  = qMin(maxWorkers, m_batchTotal);

    for (int w = 0; w < workerCount; ++w) {
        // 任务轮转均分: worker w 处理下标 w, w+workerCount, ... 的任务
        QVector<PassPredictWorker::Job> slice;
        for (int i = w; i < m_batchTotal; i += workerCount)
            slice.append(jobs[i]);

        auto thread = std::make_unique<QThread>(this);
        auto worker = std::make_shared<PassPredictWorker>();
        worker->moveToThread(thread.get());

        // 成功: 汇总结果并计数
        connect(worker.get(), &PassPredictWorker::resultReady, this,
                [this, seq](QVector<PassEvent> passes) {
                    if (seq != m_batchSeq) return;   // 旧批次迟到信号: 丢弃
                    
                    QMutexLocker locker(&m_batchMutex);
                    // std::move: PassEvent 含采样向量, 拷贝即整段深拷贝,
                    // 千颗级大组下这是纯开销
                    m_batchPasses.append(std::move(passes));
                    int completed = m_batchCompleted.fetchAndAddRelaxed(1) + 1;
                    emit batchProgress(completed, m_batchTotal);
                    if (completed == m_batchTotal) {
                        locker.unlock();  // 在调用 finishBatch 前解锁
                        finishBatch();
                    }
                });

        // 失败: 转发错误并计数（否则批量永远等不到完成 → 悬挂）
        connect(worker.get(), &PassPredictWorker::errorOccurred, this,
                [this, seq](QString msg) {
                    if (seq != m_batchSeq) return;   // 旧批次迟到信号: 丢弃
                    
                    QMutexLocker locker(&m_batchMutex);
                    int completed = m_batchCompleted.fetchAndAddRelaxed(1) + 1;
                    emit batchProgress(completed, m_batchTotal);
                    emit errorOccurred(msg);
                    if (completed == m_batchTotal) {
                        locker.unlock();  // 在调用 finishBatch 前解锁
                        finishBatch();
                    }
                });

        connect(worker.get(), &PassPredictWorker::resultReady, thread.get(), &QThread::quit);
        connect(worker.get(), &PassPredictWorker::errorOccurred, thread.get(), &QThread::quit);
        connect(thread.get(), &QThread::finished, worker.get(), &QObject::deleteLater);
        connect(worker.get(), &PassPredictWorker::metaInfo, this,
                [this, seq](QString model, QString epochUtc, double ageDays) {
                    // 批次收尾(resultReady 已发出)后, 同批次内迟到 worker 的
                    // 单星元信息不再转发——否则会覆盖"批量 N 颗"汇总并随机
                    // 改写历元栏（队列投递顺序无保证）
                    if (seq != m_batchSeq || m_batchFinished.load()) return;
                    emit metaInfo(model, epochUtc, ageDays);
                });

 m_activeJobs.append({thread.get(), worker, QDateTime::currentMSecsSinceEpoch()});

        connect(thread.get(), &QThread::started, worker.get(), [worker, slice]() {
            worker->runJobs(slice);
        });
        // QThread::start 返回 void: 线程创建失败时 Qt 内部致命退出，不会静默失败
        thread->start();
    }
}

void PassPredictorService::finishBatch()
{
    // 创建临时副本以减少锁的持有时间
    QVector<PassEvent> batchCopy;
    {
        QMutexLocker locker(&m_batchMutex);
        // 按可见性优先 + 最大仰角降序排列
        std::sort(m_batchPasses.begin(), m_batchPasses.end(),
            [](const PassEvent &a, const PassEvent &b) {
                if (a.visibility != b.visibility)
                    return static_cast<int>(a.visibility) < static_cast<int>(b.visibility);
                return a.maxElevDeg > b.maxElevDeg;
            });
        // 移动到临时量: 信号参数按值接收, 直接 emit 成员会深拷贝
        // 整批采样向量; 移动后主线程交付只剩队列转发那一份必要拷贝
        batchCopy = std::move(m_batchPasses);
        m_batchFinished = true;   // 收尾: 迟到的单星 metaInfo 不再转发
    }
    
    // emit 前先移动到临时量: 信号参数按值接收, 直接 emit 成员会深拷贝
    // 整批采样向量; 移动后主线程交付只剩队列转发那一份必要拷贝
    QVector<PassEvent> out = std::move(batchCopy);
    emit resultReady(std::move(out));
}

void PassPredictorService::cancelAll()
{
    // 递增批次序号: 取消后本批次迟到的结果/错误信号一律丢弃，
    // 杜绝"取消后结果又冒出来"的竞态（M5）
    m_batchSeq.fetchAndAddRelaxed(1);
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
    
    // 先尝试取消worker，确保线程能快速响应退出
    if (aj.worker) {
        aj.worker->cancel();
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
            m_orphans.append({aj.thread, aj.worker, QDateTime::currentMSecsSinceEpoch()});
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
            aj.worker.reset();  // worker 由 shared_ptr 自动管理
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
    aj.worker.reset();  // worker 已通过 finished → deleteLater 被清理
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
    m_reapTimer = std::make_unique<QTimer>(this);
    m_reapTimer->setInterval(500);  // 缩短间隔，提高响应速度
    connect(m_reapTimer.get(), &QTimer::timeout, this, &PassPredictorService::reapOrphans);
    m_reapTimer->start();
}

void PassPredictorService::reapOrphans()
{
    if (m_orphans.isEmpty()) return;
    
    // 按创建时间排序，优先处理旧的孤儿线程
    std::sort(m_orphans.begin(), m_orphans.end(),
        [](const ActiveJob &a, const ActiveJob &b) {
            return a.createTime < b.createTime;
        });
    
    for (int i = m_orphans.size() - 1; i >= 0; --i) {
        ActiveJob &aj = m_orphans[i];
        QThread *t = aj.thread;
        
        if (!t) {
            // 无效条目，清理
            aj.worker.reset();
            m_orphans.removeAt(i);
            continue;
        }
        
        if (!t->isRunning()) {
            // 已自然退出（或已回收）: 补 deleteLater（finished→worker 删除
            // 已由线程内连接完成），线程对象由主线程事件循环删除
            t->deleteLater();
            aj.thread = nullptr;
            aj.worker.reset();
            m_orphans.removeAt(i);
            continue;
        }
        
        // 尝试协作退出
        t->quit();
        
        // 根据线程年龄调整等待时间：新线程给更多时间，旧线程更快放弃
        qint64 age = QDateTime::currentMSecsSinceEpoch() - aj.createTime;
        int waitTime = qMin(200, qMax(50, 200 - static_cast<int>(age / 1000)));
        
        if (t->wait(waitTime)) {
            // 正常退出，安全删除
            t->deleteLater();
            aj.thread = nullptr;
            aj.worker.reset();
            m_orphans.removeAt(i);
        } else if (age > 10000) {  // 10秒以上的线程强制解除父子关系
            // 长时间运行的孤儿线程：强制解除父子关系，避免内存泄漏
            t->setParent(nullptr);
            aj.worker.reset();  // 清理worker引用
            // 不从列表中移除，让系统在进程退出时回收
            m_orphans.removeAt(i);
        }
        // 仍未退出但时间较短，保留待下一轮
    }
    
    // 如果所有孤儿都已处理，停止定时器
    if (m_orphans.isEmpty() && m_reapTimer) {
        m_reapTimer->stop();         // 全部回收完毕 → 停表省电
        m_reapTimer.reset();
    }
}
