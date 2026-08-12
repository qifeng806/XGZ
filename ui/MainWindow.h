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
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QTabWidget>
#include <QDateTimeEdit>
#include "core/models.h"
#include "business/PassPredictorService.h"
#include "business/TleFetcher.h"
#include "business/TleBlocks.h"

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
    void onTableRowSelected(int row, int col);
    void onMetaInfo(QString model, QString epochUtc, double ageDays);

private:
    void setupUI();
    void setupConnections();
    void populateTable(const QVector<PassEvent> &passes);
    // 从当前 UI 控件填充 Job 的观测站参数与计算配置
    void fillJobFromUi(PassPredictWorker::Job &job) const;
    // 公共批量启动: 解析校验 TLE 块 → 构建任务 → 启动批量预报
    void startBatchFromBlocks(const QVector<TleBlock> &blocks, const QString &origin);
    // 按"仅显示可见过境"开关过滤 m_currentPasses → m_filteredPasses 并刷新全部视图
    void applyFilterAndRefresh();
    QVector<PassEvent> m_currentPasses;    // 原始全部过境
    QVector<PassEvent> m_filteredPasses;   // 过滤后（导出用）

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

    // 状态栏
    QLabel       *m_statusModel;
    QLabel       *m_statusEpoch;
    QLabel       *m_statusCount;
    QProgressBar *m_progressBar;

    // 结果区
    QTabWidget   *m_tabWidget;
    QTableWidget *m_passTable;
    PolarPlotWidget  *m_polarPlot;
    GroundTrackWidget *m_groundTrack;
    DopplerPlotWidget *m_dopplerPlot;

    // 业务层
    PassPredictorService *m_service;
    TleFetcher *m_tleFetcher;
};

#endif // UI_MAINWINDOW_H
