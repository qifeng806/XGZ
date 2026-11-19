#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H
// =============================================================================
// MainWindow.h — 主窗口
// 四页签: 过境列表 / 极坐标图 / 地面轨迹图 / 逐秒数据
// 支持单星和批量模式，CSV/KML 导出
// =============================================================================
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QTableView>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QTabWidget>
#include <QDateTimeEdit>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QAtomicInt>
#include <memory>
#include "core/DataTypes.h"
#include "core/ErrorHandler.h"
#include "business/PassPredictorService.h"
#include "business/TleFetcher.h"
#include "business/TleBatchParser.h"
#include "PassTableModel.h"
#include "core/Propagator.h"
#include "core/OrbitCalculator.h"
#include "core/CoordTransform.h"
#include "core/Observer.h"
#include <QStandardItemModel>

QT_BEGIN_NAMESPACE
class PolarPlotWidget;
class GroundTrackWidget;
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    // 关窗前先协作式取消在途计算/下载, 避免析构链中等待线程导致的冻结感
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPredict();
    void onBatchImport();
    void onTleDownloaded(QString rawText, QString groupId);
    void onTleError(QString message);
    void onCancel();
    void onClear();
    void onExportCSV();
    void onExportKML();
    void onResultsReady(QVector<PassEvent> passes);
    void onError(QString msg);
    void onTableRowSelected(int row, int col);
    void showDetailForPass(int row);
    void onMetaInfo(QString model, QString epochUtc, double ageDays);

    // 统一错误处理: ErrorHandler 信号接入（错误必然上报, 批量模式只入汇总）
    void onErrorOccurred(const ErrorInfo &error);
    void onErrorRecoveryAttempt(const ErrorInfo &error, RecoveryAction action);
    void onErrorRecoveryResult(const ErrorInfo &error, bool success);

private:
    void setupUI();
    void setupConnections();
    void disconnectAllConnections();
    void populateTable(const QVector<const PassEvent *> &passes);
    // 把"当前选中过境"同步到全部绘图控件（极坐标 + 地面轨迹）。
    // 必须集中一处调用: 逐个手写 setSelectedPass 曾漏掉地面轨迹,
    // 导致刷新后地面轨迹高亮到另一颗过境
    void syncSelectedPass(int row);
    // 过境身份键（卫星名 + 入境时刻）: 行序变化后按身份找回原选中行
    static QString keyOfPass(const PassEvent *p);
    // 从当前 UI 控件填充 Job 的观测站参数与计算配置
    void fillJobFromUi(PassPredictWorker::Job &job) const;
    // 在线更新 TLE 的唯一实现（bypassCache=true → 忽略本地缓存强制联网;
    // 供左键常规下载与 Ctrl+点击/右键菜单的强制刷新共用）
    void forceFetchTle(bool bypassCache);
    // 起止时间合法性校验（结束必须晚于开始；否则 hoursAhead=0 会静默算出 0 条过境）
    bool ensureValidTimeWindow();
    // 公共批量启动: 解析校验 TLE 块 → 构建任务 → 启动批量预报。
    // 解析在 QtConcurrent 线程池执行（startBatchFromBlocks 异步化）,
    // 完成后经 startBatchParsed 在主线程继续
    void startBatchFromBlocks(const QVector<TleBlock> &blocks, const QString &origin);
    void startBatchParsed(const QVector<PassPredictWorker::Job> &jobs, int failed,
                          int lenIssues, int fmtIssues, const QString &origin);
    // 按"仅显示可见过境"开关过滤 m_currentPasses → m_filteredPasses 并刷新全部视图。
    // preserveKey/preserveScroll: 调用方在共享数据被覆盖【之前】预快照的
    // 选中身份键与滚动位置（如 onResultsReady —— 覆盖后 m_filteredPasses
    // 旧指针即悬垂, 函数内部无法再安全读取）; 缺省时由函数内部从仍有效的
    // m_filteredPasses 快照（过滤开关/时区切换路径）
    void applyFilterAndRefresh(const QString &preserveKey = QString(),
                               int preserveScroll = -1);
    // 统一错误处理接线（ctor 末尾调用）
    void setupErrorHandling();
    // 网络/严重错误专项处理与恢复动作
    void handleNetworkError(const ErrorInfo &error);
    void handleCriticalError(const ErrorInfo &error);
    void retryNetworkOperation(const ErrorInfo &error);
    void resetToDefaultConfig();
    // 状态栏标签: 同步文本与悬浮提示（文本过长时完整内容可悬停查看）
    void setStatusText(QLabel *label, const QString &text);
    // 状态栏去重刷新: 连续相同文本只刷一次（错误风暴节流, 供错误/恢复信号共用）
    void showStatusDedup(const QString &text);
    QVector<PassEvent> m_currentPasses;         // 原始全部过境（唯一所有权）
    QVector<const PassEvent *> m_filteredPasses; // 过滤后（共享指针, 零深拷贝）

    // 导出异步化: 写盘在 QtConcurrent 线程池直接读共享过境指针执行
    //（导出期间一切数据变更入口被锁定, 数据不可变, 无需深拷贝快照）
    enum class ExportKind { Csv, Kml };
    // 导出范围: 表格中有选中行 → 只导出这些过境; 无选中行 → 全部（过滤后的）过境。
    // rowsOut 回传被导出过境在 m_filteredPasses 中的下标（详情文件名沿用 下标+1，
    // 与表格行号一致）；scopeOut 回传范围说明文本（状态栏提示）。
    // 返回值: true = 主导出任务**已成功启动**（m_exportLock 已持有）；
    //         false = 被拒绝/无数据（调用方不得据此派生后续动作, 例如逐秒详情导出）
    bool startExport(ExportKind kind, const QString &filePath,
                     QVector<int> *rowsOut = nullptr, QString *scopeOut = nullptr);
    // 表格中选中的过境行（升序去重）；返回空表示"无选中 → 全部"
    QVector<int> selectedPassRows() const;
    void onExportFinished();
    // 计算/下载在途或导出进行中统一禁用导出入口
    void setExportEnabled(bool on);
    std::unique_ptr<QFutureWatcher<QString>> m_exportWatcher;
    QFuture<void> m_detailCsvFuture;   // detail CSV 后台导出（fire-and-forget → 跟踪等待）
    
    // 线程同步机制
    // 注: 曾有 QWaitCondition/QAtomicInt 计数器脚手架, 但全部增减均发生在
    // GUI 线程槽内, 不存在并发竞争, 属误导性死重量, 已移除（第9轮审查 A6）
    mutable QMutex m_dataMutex;           // 保护共享数据的互斥锁
    std::atomic<bool> m_isClosing{false}; // 窗口关闭标志（atomic: 跨线程无锁读）
    // 共享过境数据"换血中"深度计数（>0 = 有 onResultsReady 正处于
    // "释放锁等待逐秒任务 → 重新持锁交换数据"的窗口内）。
    // 用途: 该窗口内 m_dataMutex **未被持有**且共享数据仍是旧值, 任何会改写
    // m_currentPasses/m_filteredPasses 或重启逐秒计算的槽（onClear /
    // showDetailForPass）都必须短路, 否则"清空"会被随后的数据交换静默撤销。
    // 用计数而非 bool: 嵌套的 onResultsReady 可能重叠在同一窗口上, bool 会被
    // 先返回者提前清零。
    // 与 m_resultsGeneration 的分工: 世代号负责"后到的结果接管"（onResultsReady
    // 之间是 newest-wins, **不**用本计数短路）; 本计数只保护其它改写槽。
    std::atomic<int> m_sharedDataWaitDepth{0};
    QAtomicInt m_exportLock;             // 导出锁，防止并发导出
    std::unique_ptr<QFutureWatcher<void>> m_detailWatcher;  // 逐秒详情计算 watcher
    QAtomicInt m_detailCalcCancel;       // 逐秒计算取消标志（1=取消）
    QAtomicInt m_detailGeneration;       // 逐秒计算世代号（每次新计算 +1，finished 回调校验）
    QAtomicInt m_resultsGeneration;      // 结果处理世代号（onResultsReady 重入守卫）
    QAtomicInt m_exportCancel;           // 导出取消标志（1=取消, closeEvent 置位）
    QAtomicInt m_detailCsvCancel;        // 详情 CSV 导出取消标志（1=取消, closeEvent 置位）

    // 输入控件
    QPlainTextEdit  *m_tleInput1;
    QPlainTextEdit  *m_tleInput2;
    QDoubleSpinBox  *m_latSpin;
    QDoubleSpinBox  *m_lonSpin;
    QDoubleSpinBox  *m_altSpin;
    QDoubleSpinBox  *m_maskSpin;
    QComboBox       *m_timezoneCombo;
    QComboBox       *m_darkSkyCombo;
    QComboBox       *m_gravModelCombo;
    QComboBox       *m_tleGroupCombo;
    QCheckBox       *m_visibleOnlyCheck;
    QDateTimeEdit   *m_startTimeEdit;
    QDateTimeEdit   *m_endTimeEdit;

    // 控制按钮
    QPushButton *m_predictBtn;
    QPushButton *m_batchImportBtn;
    QPushButton *m_onlineUpdateBtn;
    QPushButton *m_cancelBtn;
    QPushButton *m_exportCsvBtn;
    QPushButton *m_exportKmlBtn;
    QPushButton *m_clearBtn;
    QCheckBox   *m_exportDetailCheck;  // "包含逐秒详情" 导出选项

    // 批量模式状态
    std::atomic<bool> m_batchMode{false};  // 当前是否为批量预报（atomic: 跨线程无锁读）
    bool m_batchParsePending = false; // 批量后台解析在途（TleFetcher::finished 收尾守卫用）
    QStringList m_batchErrors;        // 批量中各任务错误信息（汇总显示）
    // onErrorOccurred 状态栏去重: 连续相同消息只刷一次, 防批量失败风暴逐条重绘
    QString m_lastErrorStatusText;
    // 本次任务实际使用的观测站坐标（预报发起时锁定, 绘图地面标记用;
    // 防止预报后用户改动输入框导致标记漂移与结果不一致）
    double m_taskLatDeg = 0.0;
    double m_taskLonDeg = 0.0;
    double m_taskAltKm = 0.05;
    double m_taskMaskDeg = 10.0;
    // 本次任务使用的重力模型（与 UI 默认一致 = STK 的 WGS-72; 详情视图逐秒
    // 重传播必须复用同一模型, 否则表格/极坐标与详情表口径不一致）
    GravityModel m_taskGravityModel = GravityModel::WGS72;

    // 批量模式的 TLE 时效汇总（onMetaInfo 逐星累积, 状态栏给出批量级提示）:
    // 批量下不逐星显示历元, 但"数据过期"这类风险必须让用户看到
    double m_batchMaxAgeDays = 0.0;   // 本批最旧 TLE 的龄期（天）
    int    m_batchStaleCount = 0;     // 超过 kStaleAgeDays 的颗数
    int    m_batchMetaCount  = 0;     // 已回报元信息的颗数

    // 状态栏
    QLabel       *m_statusModel;
    QLabel       *m_statusEpoch;
    QLabel       *m_statusCount;
    QProgressBar *m_progressBar;

    // 结果区
    QTabWidget   *m_tabWidget;
    QTableView   *m_passTable;      // 懒加载视图: 仅请求可见单元格
    PassTableModel *m_tableModel;
    PolarPlotWidget  *m_polarPlot;
    GroundTrackWidget *m_groundTrack;
    QTableView   *m_detailTable;    // 逐秒数据页签
    QStandardItemModel *m_detailModel;

    // 业务层 - 使用智能指针管理生命周期
    std::unique_ptr<PassPredictorService> m_service;
    std::unique_ptr<TleFetcher> m_tleFetcher;
};

#endif // UI_MAINWINDOW_H
