#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H
// =============================================================================
// MainWindow.h — 主窗口
// 三页签: 过境列表 / 极坐标图 / 地面轨迹图
// 支持单星和批量模式，CSV/KML/JSON 导出
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
#include <QWaitCondition>
#include <memory>
#include "core/models.h"
#include "core/ErrorHandler.h"
#include "business/PassPredictorService.h"
#include "business/TleFetcher.h"
#include "business/TleBlocks.h"
#include "PassTableModel.h"

QT_BEGIN_NAMESPACE
class PolarPlotWidget;
class GroundTrackWidget;
class DopplerPlotWidget;
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
    void onOnlineUpdate();
    void onTleDownloaded(QString rawText, QString groupId);
    void onTleError(QString message);
    void onCancel();
    void onClear();
    void onExportCSV();
    void onExportKML();
    void onExportJSON();
    void onResultsReady(QVector<PassEvent> passes);
    void onError(QString msg);
    // TLE 校验和警告: 非致命, 仅状态栏去重提示
    void onChecksumWarning(QString satelliteId);
    void onTableRowSelected(int row, int col);
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
    // 从当前 UI 控件填充 Job 的观测站参数与计算配置
    void fillJobFromUi(PassPredictWorker::Job &job) const;
    // 公共批量启动: 解析校验 TLE 块 → 构建任务 → 启动批量预报。
    // 解析在 QtConcurrent 线程池执行（startBatchFromBlocks 异步化）,
    // 完成后经 startBatchParsed 在主线程继续
    void startBatchFromBlocks(const QVector<TleBlock> &blocks, const QString &origin);
    void startBatchParsed(const QVector<PassPredictWorker::Job> &jobs, int failed,
                          int lenIssues, int fmtIssues, const QString &origin);
    // 按"仅显示可见过境"开关过滤 m_currentPasses → m_filteredPasses 并刷新全部视图
    void applyFilterAndRefresh();
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
    enum class ExportKind { Csv, Kml, Json };
    void startExport(ExportKind kind, const QString &filePath);
    void onExportFinished();
    // 计算/下载在途或导出进行中统一禁用导出入口
    void setExportEnabled(bool on);
    std::unique_ptr<QFutureWatcher<QString>> m_exportWatcher;
    
    // 线程同步机制
    mutable QMutex m_dataMutex;           // 保护共享数据的互斥锁
    QWaitCondition m_dataReadyCondition;  // 数据就绪条件变量
    QAtomicInt m_uiUpdateCounter;        // UI更新计数器，用于防止竞态
    bool m_isClosing;                     // 窗口关闭标志
    QAtomicInt m_exportLock;             // 导出锁，防止并发导出

    // 输入控件
    QPlainTextEdit  *m_tleInput1;
    QPlainTextEdit  *m_tleInput2;
    QDoubleSpinBox  *m_latSpin;
    QDoubleSpinBox  *m_lonSpin;
    QDoubleSpinBox  *m_altSpin;
    QDoubleSpinBox  *m_maskSpin;
    QSpinBox        *m_hoursSpin;
    QComboBox       *m_timezoneCombo;
    QComboBox       *m_darkSkyCombo;
    QComboBox       *m_tleGroupCombo;
    QDoubleSpinBox  *m_freqSpin;
    QDoubleSpinBox  *m_stdMagSpin;   // 固有星等（1000 km）: 无目录匹配时回退值
    QCheckBox       *m_visibleOnlyCheck;
    QDateTimeEdit   *m_startTimeEdit;

    // 控制按钮
    QPushButton *m_predictBtn;
    QPushButton *m_batchImportBtn;
    QPushButton *m_onlineUpdateBtn;
    QPushButton *m_cancelBtn;
    QPushButton *m_exportCsvBtn;
    QPushButton *m_exportKmlBtn;
    QPushButton *m_exportJsonBtn;
    QPushButton *m_clearBtn;

    // 批量模式状态
    bool m_batchMode = false;         // 当前是否为批量预报
    QStringList m_batchErrors;        // 批量中各任务错误信息（汇总显示）
    // onErrorOccurred 状态栏去重: 连续相同消息只刷一次, 防批量失败风暴逐条重绘
    QString m_lastErrorStatusText;
    // 本次任务实际使用的观测站坐标（预报发起时锁定, 绘图地面标记用;
    // 防止预报后用户改动输入框导致标记漂移与结果不一致）
    double m_taskLatDeg = 0.0;
    double m_taskLonDeg = 0.0;

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
    DopplerPlotWidget *m_dopplerPlot;

    // 业务层 - 使用智能指针管理生命周期
    std::unique_ptr<PassPredictorService> m_service;
    std::unique_ptr<TleFetcher> m_tleFetcher;
};

#endif // UI_MAINWINDOW_H
