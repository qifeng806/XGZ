// =============================================================================
// PassPredictorService.cpp — 预报调度实现（修复版：正确的线程生命周期管理）
// =============================================================================
#include "PassPredictorService.h"
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <limits>

PassPredictorService::PassPredictorService(QObject *parent)
    : QObject(parent), m_batchCompleted(0), m_batchSeq(0), m_batchFinished(false) {}

PassPredictorService::~PassPredictorService()
{
    // 析构路径: 带超时等待，防止窗口关闭时被长时间批量任务卡死。
    // 协作取消使任务通常毫秒级退出；超时(极端情况)后线程在后台自然后退，
    // 进程退出时由系统回收，绝不在主线程无限阻塞。
    cleanupAll(2000);

    // 孤儿线程兜底: cleanupAll 已连接 finished→deleteLater 且调用过 cancel，
    // 此处不再冗余 quit/wait/deleteLater —— 仅解除父子关系避免析构 UB
    m_reapTimer.reset();
    for (auto &aj : m_orphans) {
        QThread *t = aj.thread;
        if (!t) continue;
        t->setParent(nullptr);
        aj.worker.reset();
        aj.thread = nullptr;
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
    auto worker = std::make_shared<PassPredictWorker>();
    worker->moveToThread(thread);

    // resultReady → quit thread
    connect(worker.get(), &PassPredictWorker::resultReady, thread, &QThread::quit);
    // errorOccurred → also quit thread（修复：之前不 quit 导致线程泄漏）
    connect(worker.get(), &PassPredictWorker::errorOccurred, thread, &QThread::quit);
    // jobSkipped → also quit thread（修复：恢复跳过路径此前无信号触发 quit,
    // 线程永久驻留 exec() 空转, 2s 后被孤儿回收、10s 后泄漏——单星模式下
    // 每跳过一次即泄漏一个线程）
    connect(worker.get(), &PassPredictWorker::jobSkipped, thread, &QThread::quit);
    // 注意: 不得用 finished→deleteLater 删 worker——线程结束时即释放对象,
    // 但 m_activeJobs/转发 lambda 的 shared_ptr 仍引用它, 下次 cleanupAll→
    // cancelAll 会对已释放对象调用 cancel()（悬垂指针 UAF, 析构即崩溃）。
    // worker 生命周期由 shared_ptr 全权管理（cleanupJob 中 reset 释放）
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
    // 被自动恢复策略跳过（WARNING 级告警走跳过路径）: 单星模式必须告知用户,
    // 否则"计算中"状态永不结束
    connect(worker.get(), &PassPredictWorker::jobSkipped, this,
            [this, seq](QString satelliteId, QString reason) {
                if (seq != m_batchSeq) return;
                emit errorOccurred(QString("卫星 %1 已跳过: %2")
                                       .arg(satelliteId, reason));
            });
    connect(worker.get(), &PassPredictWorker::metaInfo, this,
            [this, seq](QString model, QString epochUtc, double ageDays) {
                if (seq != m_batchSeq) return;
                emit metaInfo(model, epochUtc, ageDays);
            });

    m_activeJobs.append({thread, worker, QDateTime::currentMSecsSinceEpoch()});

    // 启动线程（在线程 started 信号中调用 worker->run）
    connect(thread, &QThread::started, worker.get(), [worker, job]() {
        worker->run(job);
    });
    // QThread::start() 返回 void, 但**并非**不会失败: 底层线程创建失败时 Qt 只
    // 置 running=false 并发一次 finished()（进程不受影响），此后本任务不再产生
    // 任何 worker 信号 → UI 永久停在"计算中"。这里显式检测并复位。
    thread->start();
    if (!thread->isRunning()) {
        // 立即回收残留 ActiveJob, 避免用户连续失败多次时槽位累积
        emit errorOccurred("无法创建计算线程，任务未启动");
        cleanupJob(m_activeJobs.last(), 2000);
    }
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
        m_batchPasses.reserve(qMin<qsizetype>(static_cast<qint64>(m_batchTotal) * 16, 400000));
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

        // 堆分配 QThread: 生命周期归 cleanupJob/孤儿回收管理（deleteLater）,
        // 绝不可用栈/unique_ptr——作用域结束即销毁仍运行的 QThread 是未定义行为
        auto *thread = new QThread(this);
        auto worker = std::make_shared<PassPredictWorker>();
        worker->moveToThread(thread);

        // 成功: 汇总结果并计数
        connect(worker.get(), &PassPredictWorker::resultReady, this,
                [this, seq](QVector<PassEvent> passes) {
                    if (seq != m_batchSeq) return;   // 旧批次迟到信号: 丢弃
                    
                    bool batchDone = false;
                    {
                        QMutexLocker locker(&m_batchMutex);
                        m_batchPasses.append(std::move(passes));
                        int completed = m_batchCompleted.fetchAndAddRelaxed(1) + 1;
                        // 用 >= 而非 ==: 一旦出现重复计数（如同一任务在
                        // emit 分配阶段抛 bad_alloc 后由 catch 再补发一次），
                        // == 会永久错失相等点 → resultReady 永不发出（批量悬挂）
                        batchDone = (completed >= m_batchTotal);
                    }
                    emit batchProgress(m_batchCompleted.loadAcquire(), m_batchTotal);
                    if (batchDone) finishBatch();
                });

        // 失败: 转发错误并计数（否则批量永远等不到完成 → 悬挂）
        connect(worker.get(), &PassPredictWorker::errorOccurred, this,
                [this, seq](QString msg) {
                    if (seq != m_batchSeq) return;   // 旧批次迟到信号: 丢弃
                    
                    bool batchDone = false;
                    {
                        QMutexLocker locker(&m_batchMutex);
                        int completed = m_batchCompleted.fetchAndAddRelaxed(1) + 1;
                        // 用 >= 而非 ==: 一旦出现重复计数（如同一任务在
                        // emit 分配阶段抛 bad_alloc 后由 catch 再补发一次），
                        // == 会永久错失相等点 → resultReady 永不发出（批量悬挂）
                        batchDone = (completed >= m_batchTotal);
                    }
                    emit batchProgress(m_batchCompleted.loadAcquire(), m_batchTotal);
                    emit errorOccurred(msg);
                    if (batchDone) finishBatch();
                });

        // 被自动恢复策略跳过（WARNING 级告警走跳过路径）: 不报错但必须计数,
        // 否则 completed 永远 < total → resultReady 永不发出（批量悬挂）
        connect(worker.get(), &PassPredictWorker::jobSkipped, this,
                [this, seq](QString satelliteId, QString reason) {
                    if (seq != m_batchSeq) return;   // 旧批次迟到信号: 丢弃
                    
                    bool batchDone = false;
                    {
                        QMutexLocker locker(&m_batchMutex);
                        int completed = m_batchCompleted.fetchAndAddRelaxed(1) + 1;
                        // 用 >= 而非 ==: 一旦出现重复计数（如同一任务在
                        // emit 分配阶段抛 bad_alloc 后由 catch 再补发一次），
                        // == 会永久错失相等点 → resultReady 永不发出（批量悬挂）
                        batchDone = (completed >= m_batchTotal);
                    }
                    emit batchProgress(m_batchCompleted.loadAcquire(), m_batchTotal);
                    emit errorOccurred(QString("卫星 %1 已跳过: %2")
                                           .arg(satelliteId, reason));
                    if (batchDone) finishBatch();
                });

        connect(worker.get(), &PassPredictWorker::resultReady, thread, &QThread::quit);
        connect(worker.get(), &PassPredictWorker::errorOccurred, thread, &QThread::quit);
        // 同 startJob: 恢复跳过路径必须 quit, 否则切片全部被跳过的 worker
        // 线程驻留空转（finishBatch 后 cleanupAll join 阻塞 2s, 10s 后泄漏）
        connect(worker.get(), &PassPredictWorker::jobSkipped, thread, &QThread::quit);
        // 同 startJob: worker 生命周期归 shared_ptr, 不得 finished→deleteLater
        // （否则线程结束时对象被释放, m_activeJobs 引用悬垂 → cancelAll UAF）
        connect(worker.get(), &PassPredictWorker::metaInfo, this,
                [this, seq](QString model, QString epochUtc, double ageDays) {
                    // 批次收尾(resultReady 已发出)后, 同批次内迟到 worker 的
                    // 单星元信息不再转发——否则会覆盖"批量 N 颗"汇总并随机
                    // 改写历元栏（队列投递顺序无保证）
                    if (seq != m_batchSeq || m_batchFinished.load()) return;
                    emit metaInfo(model, epochUtc, ageDays);
                });

        m_activeJobs.append({thread, worker, QDateTime::currentMSecsSinceEpoch()});

        connect(thread, &QThread::started, worker.get(), [worker, slice]() {
            worker->runJobs(slice);
        });
        // 同上: start() 失败时本切片既不会 resultReady 也不会 errorOccurred/
        // jobSkipped, completed 永不达 total → 批量永久悬挂（进度条卡住）。
        // 按"整片失败"一次性补计数并在最后一片时正常收尾。
        thread->start();
        if (!thread->isRunning() && seq == m_batchSeq) {   // 已取消则不补计数
            const int n = slice.size();
            int completed = 0;
            {
                QMutexLocker locker(&m_batchMutex);
                for (int k = 0; k < n; ++k)
                    completed = m_batchCompleted.fetchAndAddRelaxed(1) + 1;
            }
            emit batchProgress(m_batchCompleted.loadAcquire(), m_batchTotal);
            emit errorOccurred(QString("无法创建计算线程，%1 颗卫星未执行").arg(n));
            if (completed >= m_batchTotal) finishBatch();
        }
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
    
    // emit 按值接收: 移动后主线程交付只剩队列转发那一份必要拷贝
    emit resultReady(std::move(batchCopy));
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
    // 立即释放已累积的批量结果: 取消后这些过境不会再被交付（seq 守卫全部丢弃）,
    // 若留到下次 startBatch 才 clear, 期间最多 400000 条 PassEvent（长窗口大组
    // 可达数百 MB）会一直驻留。finishBatch 已 move 走内容时此处为空操作。
    {
        QMutexLocker locker(&m_batchMutex);
        m_batchPasses.clear();
        m_batchPasses.squeeze();
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
            : aj.thread->wait();   // ≤0: 无限等待（与头文件契约一致）
            
        if (!exited) {
            // 超时: 线程仍在运行（极端情况）。不等了——
            // 移入孤儿列表交给后台定时器二次回收（进程存活期间最终
            // 全部回收成功），而不是永久泄漏或即使执行 delete 正在运行
            // 的 QThread（未定义行为）。
            // 连接 finished→deleteLater: 线程自然退出后自动回收，防止
            // reapCount 超限后 removeAt 移除条目但 QThread 对象泄漏
            QObject::connect(aj.thread, &QThread::finished,
                             aj.thread, &QObject::deleteLater);
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
    aj.worker.reset();  // worker 生命周期归 shared_ptr（cleanupJob 中 reset 释放）
    return true;
}

void PassPredictorService::cleanupAll(int waitTimeoutMs)
{
    // 先取消，给线程时间自然退出
    cancelAll();
    // 全局截止时间（M7）: 避免逐线程串行等待在任务多时总时长 N×timeout。
    // 所有线程共享同一剩余时间预算，超时后后续线程移入孤儿列表由定时器回收。
    // waitTimeoutMs<=0 表示无限等待: 不设全局截止时间, 逐线程 wait(-1)
    const qint64 deadline = (waitTimeoutMs > 0)
        ? QDateTime::currentMSecsSinceEpoch() + waitTimeoutMs
        : std::numeric_limits<qint64>::max();
    for (auto &aj : m_activeJobs) {
        int remaining = waitTimeoutMs;
        if (waitTimeoutMs > 0) {
            remaining = static_cast<int>(
                deadline - QDateTime::currentMSecsSinceEpoch());
        } else {
            remaining = std::numeric_limits<int>::max();  // 无限等待: cleanupJob 内部 wait(INT_MAX)
        }
        if (remaining > 0) {
            cleanupJob(aj, remaining);
        } else if (aj.thread && aj.thread->isRunning()) {
            // 预算耗尽: 移入孤儿列表由 reapOrphans 回收，避免 wait(0) 无限阻塞
            QObject::connect(aj.thread, &QThread::finished,
                             aj.thread, &QObject::deleteLater);
            m_orphans.append({aj.thread, aj.worker,
                              QDateTime::currentMSecsSinceEpoch()});
            aj.thread = nullptr;
            aj.worker.reset();
        } else if (aj.thread && !aj.thread->isRunning()) {
            // 预算耗尽但线程已自然结束: 直接清理，避免资源泄漏
            aj.thread->deleteLater();
            aj.thread = nullptr;
            aj.worker.reset();
        }
    }
    // 仅在确有孤儿时启动回收定时器: reapOrphans() 在空表时早退（早退分支在
    // 停表逻辑之前），无条件启动会让 500 ms 周期定时器一直空跑到进程退出
    if (!m_orphans.isEmpty()) startReapTimer();
    m_activeJobs.clear();
}

void PassPredictorService::startReapTimer()
{
    if (m_reapTimer) return;   // 已在运行
    m_reapTimer = std::make_unique<QTimer>(nullptr);  // 不设 parent，避免与 unique_ptr 双重所有
    m_reapTimer->setInterval(500);  // 缩短间隔，提高响应速度
    connect(m_reapTimer.get(), &QTimer::timeout, this, &PassPredictorService::reapOrphans);
    m_reapTimer->start();
}

void PassPredictorService::reapOrphans()
{
    if (m_orphans.isEmpty()) return;
    
    // 每次 tick 只处理一个孤儿，避免多个孤儿累积 wait() 阻塞主线程
    for (int i = 0; i < m_orphans.size(); ++i) {
        ActiveJob &aj = m_orphans[i];
        QThread *t = aj.thread;
        
        if (!t) {
            aj.worker.reset();
            m_orphans.removeAt(i);
            break;
        }
        
        if (!t->isRunning()) {
            t->deleteLater();
            aj.thread = nullptr;
            aj.worker.reset();
            m_orphans.removeAt(i);
            break;
        }
        
        t->quit();
        
        qint64 age = QDateTime::currentMSecsSinceEpoch() - aj.createTime;
        const int waitTime = 50;
        
        if (t->wait(waitTime)) {
            t->deleteLater();
            aj.thread = nullptr;
            aj.worker.reset();
            m_orphans.removeAt(i);
        } else if (age > 10000) {
            t->setParent(nullptr);
            aj.worker.reset();
            aj.createTime = QDateTime::currentMSecsSinceEpoch();
            ++aj.reapCount;
            if (aj.reapCount >= 5) {
                qWarning() << "reapOrphans: orphan" << aj.thread
                           << "exceeded max retries, removing";
                m_orphans.removeAt(i);
            }
        }
        break;  // 每次 tick 仅处理一个
    }
    
    // 如果所有孤儿都已处理，停止定时器
    if (m_orphans.isEmpty() && m_reapTimer) {
        // 槽内不销毁发送者 QTimer（Qt6 COW 通常安全非保证）:
        // 延迟到下一轮事件循环收尾。
        // 用 release()+deleteLater()（而非 reset() 里直接 delete）: 该延迟
        // 回调可能被随后的模态弹窗打开的**嵌套事件循环**提前执行, 而那正是
        // QTimer::timeout 的发射栈——deleteLater 的 DeferredDelete 事件不会被
        // 内层事件循环投递（与 TleFetcher::onReplyFinished 的看门狗同一处理）
        QTimer::singleShot(0, this, [this]() {
            if (m_orphans.isEmpty() && m_reapTimer) {
                m_reapTimer->stop();   // 全部回收完毕 → 停表省电
                m_reapTimer.release()->deleteLater();
            }
        });
    }
}
