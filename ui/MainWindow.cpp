// =============================================================================
// MainWindow.cpp — 主窗口完整实现
// =============================================================================
#include "MainWindow.h"
#include "PolarPlotWidget.h"
#include "GroundTrackWidget.h"
#include "DopplerPlotWidget.h"
#include "business/TleBlocks.h"
#include "core/TleParser.h"
#include "core/CoordTransform.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScrollBar>
#include <QSplitter>
#include <QTimeZone>
#include <QCloseEvent>

// ============================================================================
// 批量 TLE 导入辅助（文件局部）
// ============================================================================
namespace {

// 批量导入对话框：多行 TLE 文本 + 从文件加载
class BatchTleDialog : public QDialog {
    Q_OBJECT
public:
    explicit BatchTleDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("批量导入 TLE");
        resize(760, 520);

        auto *lay = new QVBoxLayout(this);
        auto *hint = new QLabel(
            "每颗卫星：可选名称行 + TLE 两行根数（也可直接两行），可粘贴多颗。\n"
            "支持从 Celestrak / NORAD 下载的文本块（含注释头时自动忽略）。");
        hint->setWordWrap(true);
        lay->addWidget(hint);

        m_edit = new QPlainTextEdit;
        m_edit->setPlaceholderText(
            "ISS (ZARYA)\n"
            "1 25544U 98067A   24083.56167013  .00012345  00000-0  24655-3 0  9994\n"
            "2 25544  51.6423  23.4567 0004567 123.4567 236.7890 15.50012345678901\n"
            "\n"
            "STARLINK-1001\n"
            "1 44713U 19074A   24083.91234567  .00000000  00000-0  00000+0 0  1234\n"
            "2 44713  53.0541  88.1234 0001234  45.6789 314.5678 15.06323456789012");
        lay->addWidget(m_edit, 1);

        auto *btnRow = new QHBoxLayout;
        auto *loadBtn = new QPushButton("从文件加载");
        auto *okBtn = new QPushButton("开始批量预报");
        okBtn->setDefault(true);
        auto *cancelBtn = new QPushButton("取消");
        btnRow->addWidget(loadBtn);
        btnRow->addStretch();
        btnRow->addWidget(okBtn);
        btnRow->addWidget(cancelBtn);
        lay->addLayout(btnRow);

        connect(loadBtn, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this, "选择 TLE 文件", QString(),
                "Text Files (*.txt);;All Files (*)");
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QMessageBox::warning(this, "批量导入", "无法读取文件: " + path);
                return;
            }
            m_edit->setPlainText(QString::fromUtf8(f.readAll()));
        });
        connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    }

    QString text() const { return m_edit->toPlainText(); }
    void setText(const QString &t) { m_edit->setPlainText(t); }

private:
    QPlainTextEdit *m_edit;
};

// 表头悬浮提示（M3 修复: setHorizontalHeaderLabels 会重建表头项并丢弃
// 已有 tooltip, 故每次重建表头后必须重新应用, 否则提示永久丢失）
void applyHeaderTips(QTableWidget *table)
{
    const QStringList headerTips = {
        QString(), QString(), QString(), QString(),
        "过境持续时长", "几何仰角（悬浮显示大气折射后的视仰角）",
        "TCA 时刻方位角", "光学可见 / 仅无线电 / 白天",
        "TCA 时刻太阳高度角", "TCA 时刻卫星日照状态（半影带为太阳圆面可见比例）",
        "几何过境内「暗天 + 被照亮」的连续可见时段",
        "入境时刻卫星星下点经纬度与高度",
        "TCA 时刻观测者至卫星斜距", "TCA 时刻多普勒频移",
        "视星等估算（Vallerie 相位模型），越小越亮",
        "综合质量评级"
    };
    for (int c = 0; c < headerTips.size(); ++c) {
        if (!headerTips[c].isEmpty() && table->horizontalHeaderItem(c))
            table->horizontalHeaderItem(c)->setToolTip(headerTips[c]);
    }
}

} // namespace

// ============================================================================
// 构建 UI
// ============================================================================
void MainWindow::setupUI()
{
    setWindowTitle("XGZ — 卫星 TLE 过境预报系统");
    resize(1280, 800);
    // 保证顶部状态栏三项文字完整展示（不被挤占截断）所需的最小宽度
    setMinimumWidth(1080);

    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    auto *mainLayout = new QVBoxLayout(centralWidget);

    // ---- 输入区 ----
    auto *inputGroup = new QGroupBox("输入参数");
    auto *inputLayout = new QGridLayout(inputGroup);

    // TLE 输入
    inputLayout->addWidget(new QLabel("TLE 行 1:"), 0, 0);
    m_tleInput1 = new QPlainTextEdit;
    m_tleInput1->setFixedHeight(36);
    m_tleInput1->setPlaceholderText(
        "1 25544U 98067A   24083.56167013  .00012345  00000-0  24655-3 0  9994");
    inputLayout->addWidget(m_tleInput1, 0, 1, 1, 5);

    inputLayout->addWidget(new QLabel("TLE 行 2:"), 1, 0);
    m_tleInput2 = new QPlainTextEdit;
    m_tleInput2->setFixedHeight(36);
    m_tleInput2->setPlaceholderText(
        "2 25544  51.6423  23.4567 0004567 123.4567 236.7890 15.50012345678901");
    inputLayout->addWidget(m_tleInput2, 1, 1, 1, 5);

    // 地面站行
    inputLayout->addWidget(new QLabel("站纬度(°):"), 2, 0);
    m_latSpin = new QDoubleSpinBox;
    m_latSpin->setRange(-90.0, 90.0);
    m_latSpin->setValue(39.9042);
    m_latSpin->setDecimals(4);
    m_latSpin->setSuffix("°N");
    inputLayout->addWidget(m_latSpin, 2, 1);

    inputLayout->addWidget(new QLabel("站经度(°):"), 2, 2);
    m_lonSpin = new QDoubleSpinBox;
    m_lonSpin->setRange(-180.0, 180.0);
    m_lonSpin->setValue(116.4074);
    m_lonSpin->setDecimals(4);
    m_lonSpin->setSuffix("°E");
    inputLayout->addWidget(m_lonSpin, 2, 3);

    inputLayout->addWidget(new QLabel("海拔(km):"), 2, 4);
    m_altSpin = new QDoubleSpinBox;
    m_altSpin->setRange(-1.0, 100.0);
    m_altSpin->setValue(0.05);
    m_altSpin->setDecimals(3);
    inputLayout->addWidget(m_altSpin, 2, 5);

    // 参数行
    inputLayout->addWidget(new QLabel("掩蔽角(°):"), 3, 0);
    m_maskSpin = new QDoubleSpinBox;
    m_maskSpin->setRange(0.0, 90.0);
    m_maskSpin->setValue(10.0);
    inputLayout->addWidget(m_maskSpin, 3, 1);

    inputLayout->addWidget(new QLabel("预报时长:"), 3, 2);
    m_hoursSpin = new QSpinBox;
    m_hoursSpin->setRange(1, 168);
    m_hoursSpin->setValue(24);
    m_hoursSpin->setSuffix(" h");
    inputLayout->addWidget(m_hoursSpin, 3, 3);

    inputLayout->addWidget(new QLabel("时区:"), 3, 4);
    m_timezoneCombo = new QComboBox;
    m_timezoneCombo->addItem("UTC+0", 0);
    m_timezoneCombo->addItem("UTC+8 (北京)", 8 * 3600);
    inputLayout->addWidget(m_timezoneCombo, 3, 5);

    // 高级参数行
    inputLayout->addWidget(new QLabel("暗天阈值:"), 4, 0);
    m_darkSkyCombo = new QComboBox;
    m_darkSkyCombo->addItem("民用 −6°", -6.0);
    m_darkSkyCombo->addItem("航海 −12°", -12.0);
    m_darkSkyCombo->addItem("天文 −18°", -18.0);
    inputLayout->addWidget(m_darkSkyCombo, 4, 1);

    inputLayout->addWidget(new QLabel("载频(MHz):"), 4, 2);
    m_freqSpin = new QDoubleSpinBox;
    m_freqSpin->setRange(1.0, 100000.0);
    m_freqSpin->setValue(437.800);
    m_freqSpin->setDecimals(3);
    inputLayout->addWidget(m_freqSpin, 4, 3);

    m_visibleOnlyCheck = new QCheckBox("仅显示可见过境");
    inputLayout->addWidget(m_visibleOnlyCheck, 4, 4, 1, 2);

    inputLayout->addWidget(new QLabel("起始时间(UTC):"), 5, 0);
    m_startTimeEdit = new QDateTimeEdit(QDateTime::currentDateTimeUtc());
    m_startTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    m_startTimeEdit->setTimeZone(QTimeZone::UTC);
    inputLayout->addWidget(m_startTimeEdit, 5, 1, 1, 3);

    inputLayout->addWidget(new QLabel("固有星等:"), 5, 4);
    m_stdMagSpin = new QDoubleSpinBox;
    m_stdMagSpin->setRange(0.0, 20.0);
    m_stdMagSpin->setValue(4.5);
    m_stdMagSpin->setDecimals(1);
    m_stdMagSpin->setToolTip(
        "卫星固有星等（1000 km、满相位，越小越亮）\n"
        "内置星等目录含 ISS/天宫/哈勃等常见亮星；\n"
        "未匹配目录的卫星使用此值估算视星等");
    inputLayout->addWidget(m_stdMagSpin, 5, 5);

    // 在线更新行
    inputLayout->addWidget(new QLabel("在线更新:"), 6, 0);
    m_tleGroupCombo = new QComboBox;
    for (const auto &g : TleFetcher::groups())
        m_tleGroupCombo->addItem(g.label, g.id);
    m_tleGroupCombo->setToolTip("从 CelesTrak 下载该组卫星的最新 TLE");
    inputLayout->addWidget(m_tleGroupCombo, 6, 1, 1, 3);
    m_onlineUpdateBtn = new QPushButton("下载 TLE");
    inputLayout->addWidget(m_onlineUpdateBtn, 6, 4, 1, 2);

    mainLayout->addWidget(inputGroup);

    // ---- 控制按钮行 ----
    auto *btnLayout = new QHBoxLayout;
    m_predictBtn = new QPushButton("▶ 开始预报");
    m_predictBtn->setStyleSheet("font-weight: bold; min-height: 32px;");
    m_batchImportBtn = new QPushButton("批量导入");
    m_cancelBtn  = new QPushButton("✕ 取消");
    m_cancelBtn->setEnabled(false);
    m_exportCsvBtn = new QPushButton("导出 CSV");
    m_exportKmlBtn = new QPushButton("导出 KML");
    m_exportJsonBtn = new QPushButton("导出 JSON");
    m_clearBtn   = new QPushButton("清空");

    btnLayout->addWidget(m_predictBtn);
    btnLayout->addWidget(m_batchImportBtn);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_exportCsvBtn);
    btnLayout->addWidget(m_exportKmlBtn);
    btnLayout->addWidget(m_exportJsonBtn);
    btnLayout->addWidget(m_clearBtn);
    btnLayout->addStretch();

    m_progressBar = new QProgressBar;
    m_progressBar->setVisible(false);
    m_progressBar->setFixedWidth(200);
    btnLayout->addWidget(m_progressBar);

    mainLayout->addLayout(btnLayout);

// ---- 状态栏 ----
    auto *statusLayout = new QHBoxLayout;
    statusLayout->setSpacing(28);
    m_statusModel = new QLabel("模型: —");
    m_statusEpoch = new QLabel("历元: —");
    m_statusCount = new QLabel("过境: 0");
    // 状态标签永不压缩（内容完整展示, 不互相挤占截断）;
    // 超长时可悬停查看全文（setStatusText 同步 tooltip）
    for (QLabel *lbl : {static_cast<QLabel *>(m_statusModel),
                        static_cast<QLabel *>(m_statusEpoch),
                        static_cast<QLabel *>(m_statusCount)}) {
        lbl->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    statusLayout->addWidget(m_statusModel);
    statusLayout->addWidget(m_statusEpoch);
    statusLayout->addWidget(m_statusCount);
    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);

    // ---- 结果区（三页签） ----
    m_tabWidget = new QTabWidget;

    // 页签 1: 过境列表
    m_passTable = new QTableWidget;
    m_passTable->setColumnCount(16);
    m_passTable->setHorizontalHeaderLabels({
        "#", "卫星名", "入境(UTC)", "出境(UTC)", "持续",
        "最大仰角", "方位角", "可见性", "太阳高度", "卫星光照",
        "可见弧段", "入境位置", "斜距", "多普勒", "星等", "质量"
    });
    m_passTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // "卫星名" 与 "可见弧段" 拉伸填充剩余空间, 其余列固定宽度, 避免最右列被拉空
    m_passTable->horizontalHeader()->setSectionResizeMode(1,  QHeaderView::Stretch);
    m_passTable->horizontalHeader()->setSectionResizeMode(10, QHeaderView::Stretch);
    const int colWidths[16] = {
        /* # */ 40, /* 卫星名 */ 120, /* 入境 */ 150, /* 出境 */ 150, /* 持续 */ 60,
        /* 最大仰角 */ 75, /* 方位角 */ 60, /* 可见性 */ 50, /* 太阳高度 */ 70, /* 卫星光照 */ 55,
        /* 可见弧段 */ 170, /* 入境位置 */ 165, /* 斜距 */ 85, /* 多普勒 */ 90, /* 星等 */ 55, /* 质量 */ 60
    };
    for (int c = 0; c < 16; ++c)
        m_passTable->setColumnWidth(c, colWidths[c]);
    m_passTable->horizontalHeader()->setMinimumSectionSize(45);

    // 表头提示（悬浮说明列语义）——由辅助函数维护，
    // 表头重建（setHorizontalHeaderLabels）后须重新应用（M3）
    applyHeaderTips(m_passTable);
    m_passTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_passTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_passTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_passTable->setAlternatingRowColors(true);
    m_passTable->verticalHeader()->setVisible(false);
    m_tabWidget->addTab(m_passTable, "过境列表");

    // 页签 2: 极坐标图
    m_polarPlot = new PolarPlotWidget;
    m_tabWidget->addTab(m_polarPlot, "极坐标图");

    // 页签 3: 多普勒曲线
    m_dopplerPlot = new DopplerPlotWidget;
    m_tabWidget->addTab(m_dopplerPlot, "多普勒曲线");

    // 页签 4: 地面轨迹图
    m_groundTrack = new GroundTrackWidget;
    m_tabWidget->addTab(m_groundTrack, "地面轨迹");

    mainLayout->addWidget(m_tabWidget, 1);
}

// ============================================================================
// 槽函数连接
// ============================================================================
void MainWindow::setupConnections()
{
    m_service = new PassPredictorService(this);
    m_tleFetcher = new TleFetcher(this);

    connect(m_predictBtn, &QPushButton::clicked, this, &MainWindow::onPredict);
    connect(m_batchImportBtn, &QPushButton::clicked, this, &MainWindow::onBatchImport);
    connect(m_onlineUpdateBtn, &QPushButton::clicked, this, &MainWindow::onOnlineUpdate);
    connect(m_tleFetcher, &TleFetcher::downloaded, this, &MainWindow::onTleDownloaded);
    connect(m_tleFetcher, &TleFetcher::fetchError, this, &MainWindow::onTleError);
    connect(m_tleFetcher, &TleFetcher::finished, this, [this]() {
        m_onlineUpdateBtn->setEnabled(true);
        m_onlineUpdateBtn->setText("下载 TLE");
        // 下载收尾: 无计算任务（m_batchMode=false）时恢复预报入口与取消按钮。
        // 不用 cancelBtn 状态作守卫: 下载期间取消按钮也处于可用状态
        if (!m_batchMode) {
            m_predictBtn->setEnabled(true);
            m_batchImportBtn->setEnabled(true);
            m_cancelBtn->setEnabled(false);
        }
    });
    connect(m_cancelBtn,  &QPushButton::clicked, this, &MainWindow::onCancel);
    connect(m_clearBtn,   &QPushButton::clicked, this, &MainWindow::onClear);
    connect(m_exportCsvBtn, &QPushButton::clicked, this, &MainWindow::onExportCSV);
    connect(m_exportKmlBtn, &QPushButton::clicked, this, &MainWindow::onExportKML);
    connect(m_exportJsonBtn, &QPushButton::clicked, this, &MainWindow::onExportJSON);

    connect(m_service, &PassPredictorService::resultReady,
            this, &MainWindow::onResultsReady);
    connect(m_service, &PassPredictorService::errorOccurred,
            this, &MainWindow::onError);
    connect(m_service, &PassPredictorService::batchProgress,
            this, [this](int completed, int total) {
                m_progressBar->setRange(0, total);
                m_progressBar->setValue(completed);
            });
    connect(m_service, &PassPredictorService::metaInfo,
            this, &MainWindow::onMetaInfo);
    connect(m_passTable, &QTableWidget::cellClicked,
            this, &MainWindow::onTableRowSelected);
    // 键盘方向键/程序化移动也会改变当前单元格, 与鼠标点击统一处理
    connect(m_passTable, &QTableWidget::currentCellChanged,
            this, [this](int row, int col, int, int) {
                onTableRowSelected(row, col);
            });
    // 切换"仅显示可见过境"时立即重新过滤并刷新表格/图表
    connect(m_visibleOnlyCheck, &QCheckBox::toggled, this, [this](bool) {
        if (m_currentPasses.isEmpty()) return;
        applyFilterAndRefresh();
    });
    // 时区切换: 无需重新计算, 重建表格时间列与表头即可（H3 修复）
    connect(m_timezoneCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_currentPasses.isEmpty()) return;
        applyFilterAndRefresh();
    });
}

// ============================================================================
// 按可见性开关过滤并刷新表格/图表/统计
// ============================================================================
void MainWindow::applyFilterAndRefresh()
{
    if (m_visibleOnlyCheck->isChecked()) {
        QVector<PassEvent> filtered;
        for (const auto &p : m_currentPasses)
            if (p.visibility == PassVisibility::Visible)
                filtered.append(p);
        m_filteredPasses = filtered;
    } else {
        m_filteredPasses = m_currentPasses;
    }
    // 刷新前保存选中行（setRowCount 会清空当前单元格, 事后恢复,
    // 过滤/时区切换/重新计算结果刷新时保留用户选择, 越界则钳到第 0 行）
    const int prevRow = (m_passTable->rowCount() > 0)
        ? m_passTable->currentRow() : -1;
    populateTable(m_filteredPasses);
    m_polarPlot->setPasses(m_filteredPasses);
    m_dopplerPlot->setPasses(m_filteredPasses);
    // 地面标记使用任务发起时的观测站坐标（非当前输入框值, 防止结果与标记漂移）
    m_groundTrack->setPasses(m_filteredPasses, m_taskLatDeg, m_taskLonDeg);

    int visCount = 0;
    for (const auto &p : m_currentPasses)
        if (p.visibility == PassVisibility::Visible) ++visCount;
    setStatusText(m_statusCount, QString("过境: %1 (可见: %2)")
        .arg(m_currentPasses.size()).arg(visCount));

    // 恢复选中: 优先保留用户原选择, 越界/无选中时钳到第 0 行
    if (m_passTable->rowCount() > 0) {
        const int row = (prevRow >= 0 && prevRow < m_passTable->rowCount())
            ? prevRow : 0;
        m_passTable->setCurrentCell(row, 0);
    }
}

void MainWindow::setStatusText(QLabel *label, const QString &text)
{
    label->setText(text);
    label->setToolTip(text);   // 文本超长时悬停查看完整内容
}

// ============================================================================
// 预报触发
// ============================================================================
void MainWindow::onPredict()
{
    QString tle1 = m_tleInput1->toPlainText().trimmed();
    QString tle2 = m_tleInput2->toPlainText().trimmed();

    if (tle1.isEmpty() || tle2.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请输入完整的 TLE 两行根数");
        return;
    }

    PassPredictWorker::Job job;
    fillJobFromUi(job);
    job.tleLine1 = tle1;
    job.tleLine2 = tle2;

    // 锁定本次任务的观测站坐标（绘图地面标记使用）
    m_taskLatDeg = m_latSpin->value();
    m_taskLonDeg = m_lonSpin->value();

    m_batchMode = false;
    m_batchErrors.clear();
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0);  // 不确定进度
    setStatusText(m_statusEpoch, "历元: —");
    m_statusEpoch->setStyleSheet(QString());

    m_service->startJob(job);
}

// ============================================================================
// 批量导入
// ============================================================================
void MainWindow::onBatchImport()
{
    BatchTleDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    int incomplete = 0;
    const auto blocks = parseTleBlocks(dlg.text(), &incomplete);
    if (blocks.isEmpty()) {
        QMessageBox::warning(this, "批量导入", "未识别到有效的 TLE 两行根数");
        return;
    }
    if (incomplete > 0) {
        QMessageBox::warning(this, "批量导入",
            QString("已解析 %1 颗卫星；另有 %2 条 TLE 不完整（缺少行1或行2），已跳过。")
                .arg(blocks.size()).arg(incomplete));
    }
    startBatchFromBlocks(blocks, "批量导入");
}

// ============================================================================
// 在线更新 TLE（CelesTrak）
// ============================================================================
void MainWindow::onOnlineUpdate()
{
    const QString groupId = m_tleGroupCombo->currentData().toString();
    if (groupId.isEmpty()) return;
    m_onlineUpdateBtn->setEnabled(false);
    m_onlineUpdateBtn->setText("下载中…");
    // 下载期间禁止发起预报/批量（防止任务与下载交错）
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    // 取消按钮在下载期间可用: onCancel 会中止在途下载
    m_cancelBtn->setEnabled(true);
    setStatusText(m_statusModel, QString("模型: 正在下载 %1 的 TLE…")
                  .arg(m_tleGroupCombo->currentText()));
    m_tleFetcher->fetchGroup(groupId);
}

// ============================================================================
// TLE 下载完成 → 解析并弹出批量导入对话框（预填文本）
// ============================================================================
void MainWindow::onTleDownloaded(QString rawText, QString groupId)
{
    int incomplete = 0;
    const auto blocks = parseTleBlocks(rawText, &incomplete);
    if (blocks.isEmpty()) {
        QMessageBox::warning(this, "在线更新",
            QString("组 [%1] 未解析到有效 TLE（可能是空数据或格式变化）")
                .arg(groupId));
        return;
    }
    if (incomplete > 0) {
        QMessageBox::warning(this, "在线更新",
            QString("组 [%1] 中有 %2 条 TLE 不完整，已跳过（其余 %3 颗正常）。")
                .arg(groupId).arg(incomplete).arg(blocks.size()));
    }
    setStatusText(m_statusModel, QString("模型: 已下载 %1 颗卫星的 TLE").arg(blocks.size()));

    BatchTleDialog dlg(this);
    dlg.setWindowTitle(QString("批量导入 — 在线下载 (%1 颗)").arg(blocks.size()));
    dlg.setText(rawText);
    if (dlg.exec() != QDialog::Accepted) return;

    startBatchFromBlocks(blocks, "在线更新");
}

// ============================================================================
// 公共批量启动: 解析校验 TLE 块 → 构建任务 → 启动批量预报
// 供"批量导入"与"在线更新下载"两条入口复用
// ============================================================================
void MainWindow::startBatchFromBlocks(const QVector<TleBlock> &blocks,
                                      const QString &origin)
{
    QVector<PassPredictWorker::Job> jobs;
    int failed = 0;
    for (const auto &b : blocks) {
        TleParser::ParseError parseErr;
        auto tleOpt = TleParser::parse(b.line1.toStdString(), b.line2.toStdString(),
                                       &parseErr);
        if (!tleOpt) { ++failed; continue; }
        PassPredictWorker::Job job;
        fillJobFromUi(job);
        job.tleLine0 = b.name;
        job.tleLine1 = b.line1;
        job.tleLine2 = b.line2;
        jobs.append(job);
    }

    if (jobs.isEmpty()) {
        QMessageBox::warning(this, origin,
            QString("共 %1 颗卫星，全部解析失败，请检查 TLE 内容")
                .arg(blocks.size()));
        return;
    }

    // 批量任务共用 UI 观测站参数, 锁定本次任务坐标（绘图地面标记使用）
    m_taskLatDeg = m_latSpin->value();
    m_taskLonDeg = m_lonSpin->value();

    m_batchMode = true;
    m_batchErrors.clear();
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, jobs.size());
    m_progressBar->setValue(0);
    setStatusText(m_statusModel, QString("模型: 批量 %1 颗").arg(jobs.size()));
    if (failed > 0)
        setStatusText(m_statusModel,
            QString("模型: 批量 %1 颗（%2 颗解析失败跳过）")
                .arg(jobs.size()).arg(failed));

    m_service->startBatch(jobs);
}

void MainWindow::onTleError(QString message)
{
    setStatusText(m_statusModel, "模型: TLE 下载失败");
    QMessageBox::warning(this, "在线更新失败", message);
}

// 从当前 UI 控件填充观测站参数与计算配置
void MainWindow::fillJobFromUi(PassPredictWorker::Job &job) const
{
    job.observerLatDeg = m_latSpin->value();
    job.observerLonDeg = m_lonSpin->value();
    job.observerAltKm  = m_altSpin->value();
    job.minElevDeg     = m_maskSpin->value();
    job.hoursAhead     = m_hoursSpin->value();
    job.startUtc       = m_startTimeEdit->dateTime();

    // 配置 OrbitCalculator 参数
    // 粗搜步长 15s: 高掩蔽角 + 低轨的短过境（窗口 1~2 分钟）也能可靠检出
    //（60s 步长会概率性漏检；每颗卫星全窗口约 0.2s，批量并行可接受）
    job.calcCfg.coarseStepSec      = 15.0;
    job.calcCfg.bisectionIters     = 18;
    job.calcCfg.enableSunAnalysis  = true;
    job.calcCfg.enableShadowCheck  = true;
    job.calcCfg.enableRefraction   = true;
    job.calcCfg.enableDoppler      = true;
    job.calcCfg.carrierFreqMHz     = m_freqSpin->value();
    job.calcCfg.enableMagnitude    = true;
    job.calcCfg.stdMagFallback     = m_stdMagSpin->value();
    job.calcCfg.darkSkyThresholdDeg = m_darkSkyCombo->currentData().toDouble();
}

void MainWindow::onCancel()
{
    // 清除全部计算线程（内部先协作式取消，毫秒级退出；
    // 2000ms 全局截止防极端情况下无限阻塞 UI）
    m_service->cleanupAll(2000);
    // 中止在途的在线 TLE 下载（无活动请求时静默无效）
    m_tleFetcher->cancel();
    m_batchMode = false;
    m_batchErrors.clear();
    // 复位状态栏: 取消后不再显示已淘汰任务的模型/历元信息
    setStatusText(m_statusModel, "模型: —");
    setStatusText(m_statusEpoch, "历元: —");
    m_statusEpoch->setStyleSheet(QString());
    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
    // 下载流程收尾（cancel() 内已 emit finished，此处为空操作保留语义清晰）
}

void MainWindow::onClear()
{
    // 清空视图的同时终止在途计算, 防止迟到的结果覆盖刚清空的界面。
    // 用 cleanupAll(2000) 而非 cancelAll: 在途线程被协作式取消后回收
    // （quit+wait), 反复"开始→清除"不残留空转线程/worker 对象
    m_service->cleanupAll(2000);
    m_tleFetcher->cancel();
    m_batchMode = false;
    m_batchErrors.clear();

    m_tleInput1->clear();
    m_tleInput2->clear();
    m_passTable->setRowCount(0);
    m_currentPasses.clear();
    m_filteredPasses.clear();   // 过滤结果同步清空（M4: 否则内存滞留过境数据）
    if (m_visibleOnlyCheck->isChecked()) {
        // 复位"仅显示可见过境"勾选（阻塞信号避免空数据下的多余刷新）
        m_visibleOnlyCheck->blockSignals(true);
        m_visibleOnlyCheck->setChecked(false);
        m_visibleOnlyCheck->blockSignals(false);
    }
    m_polarPlot->clear();
    m_dopplerPlot->clear();
    m_groundTrack->clear();
    setStatusText(m_statusModel, "模型: —");
    setStatusText(m_statusEpoch, "历元: —");
    m_statusEpoch->setStyleSheet(QString());
    setStatusText(m_statusCount, "过境: 0");
    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
}

// ============================================================================
// 关闭窗口: 先协作式取消在途任务, 消除析构链中 cleanupAll 的等待冻结感
// ============================================================================
void MainWindow::closeEvent(QCloseEvent *event)
{
    m_service->cancelAll();      // 立即置协作取消标志, 线程毫秒级退出
    m_tleFetcher->cancel();      // 中止在途下载
    event->accept();
}

// ============================================================================
// 结果处理
// ============================================================================
void MainWindow::onResultsReady(QVector<PassEvent> passes)
{
    m_currentPasses = passes;
    applyFilterAndRefresh();

    // 批量模式收尾: 汇总失败信息（若有）
    if (m_batchMode) {
        m_batchMode = false;
        if (!m_batchErrors.isEmpty()) {
            QMessageBox::warning(this, "批量预报",
                QString("%1 颗卫星预报失败:\n%2")
                    .arg(m_batchErrors.size())
                    .arg(m_batchErrors.join("\n")));
            m_batchErrors.clear();
        }
    }

    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
}

void MainWindow::onError(QString msg)
{
    if (m_batchMode) {
        // 批量模式: 不弹阻塞对话框，累计到结果时统一汇总
        m_batchErrors << msg;
        setStatusText(m_statusModel,
                      QString("模型: 批量错误 %1 项").arg(m_batchErrors.size()));
        return;
    }
    QMessageBox::warning(this, "错误", msg);
    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
}

void MainWindow::onMetaInfo(QString model, QString epochUtc, double ageDays)
{
    // 批量模式由汇总文本统一展示，跳过逐星元信息
    if (m_batchMode) return;
    setStatusText(m_statusModel, QString("模型: %1").arg(model));
    // 行业惯例: TLE 龄期 > 14 天预报误差可达数 10 秒，需醒目提示
    const bool stale = ageDays > 14.0;
    setStatusText(m_statusEpoch, QString("历元: %1（%2 天）%3")
        .arg(epochUtc).arg(ageDays, 0, 'f', 1)
        .arg(stale ? " [数据过期]" : ""));
    m_statusEpoch->setStyleSheet(stale ? "color:#d04040;" : QString());
}

void MainWindow::onTableRowSelected(int row, int col)
{
    Q_UNUSED(col);
    if (row >= 0 && row < m_passTable->rowCount()) {
        m_polarPlot->setSelectedPass(row);
        m_dopplerPlot->setSelectedPass(row);
        m_groundTrack->setSelectedPass(row);
    }
}

// ============================================================================
// 表格填充
// ============================================================================
void MainWindow::populateTable(const QVector<PassEvent> &passes)
{
    // 单次 setRowCount(n): 自动裁剪/扩充行, 不再先清零再重建
    //（滤波/时区切换反复刷新时省去全表重建的卡顿）
    m_passTable->setRowCount(passes.size());

    int tzOffsetSec = m_timezoneCombo->currentData().toInt();
    QTimeZone displayTz = (tzOffsetSec == 0)
        ? QTimeZone::UTC
        : QTimeZone(tzOffsetSec);

    // 表头随所选时区动态变化（H3: 时区下拉框此前为无效控件）
    const QString tzLabel = (tzOffsetSec == 0)
        ? QStringLiteral("UTC")
        : QStringLiteral("UTC%1%2")
              .arg(tzOffsetSec > 0 ? QStringLiteral("+") : QStringLiteral("-"))
              .arg(qAbs(tzOffsetSec) / 3600);
    if (m_passTable->horizontalHeaderItem(2)
        && m_passTable->horizontalHeaderItem(2)->text() != "入境(" + tzLabel + ")") {
        m_passTable->setHorizontalHeaderLabels({
            "#", "卫星名", "入境(" + tzLabel + ")", "出境(" + tzLabel + ")", "持续",
            "最大仰角", "方位角", "可见性", "太阳高度", "卫星光照",
            "可见弧段", "入境位置", "斜距", "多普勒", "星等", "质量"
        });
        applyHeaderTips(m_passTable);   // 表头重建后重应用悬浮提示（M3）
    }

    auto jdMsToStr = [&displayTz](int64_t jdMs) -> QString {
        // JD×86400000 毫秒 → 1970 epoch 毫秒（纯整数运算，无舍入/精度损失）
        qint64 msecs = jdMs - 210866760000000LL;  // 2440587.5 × 86400000
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC);
        if (displayTz != QTimeZone::UTC)
            dt = dt.toTimeZone(displayTz);
        return dt.toString("HH:mm:ss");
    };

    for (int i = 0; i < passes.size(); ++i) {
        const auto &p = passes[i];
        int r = i;

        auto setCell = [&](int c, const QString &text) {
            m_passTable->setItem(r, c, new QTableWidgetItem(text));
        };

        setCell(0,  QString::number(i + 1));
        setCell(1,  QString::fromStdString(p.satelliteName));
        setCell(2,  jdMsToStr(p.aosJdMs));
        setCell(3,  jdMsToStr(p.losJdMs));
        setCell(4,  QString("%1s%2")
                .arg(static_cast<int>(p.durationSec))
                .arg(p.truncated ? " *" : ""));
        // 截断过境: 过境延续至预测窗口外, 出境时刻并非真实值
        if (p.truncated) {
            auto *durItem = m_passTable->item(r, 4);
            if (durItem)
                durItem->setToolTip("过境延续至预测窗口结束时刻（真实出境在窗口外, 持续时长被截断）");
        }
        setCell(5,  QString::number(p.maxElevDeg, 'f', 1) + "°");

        // 大气折射后的视仰角（几何仰角带过 tooltip 展示，仅显示用；
        // AOS/LOS 判定仍用几何仰角）
        auto *elTipItem = m_passTable->item(r, 5);
        if (elTipItem && p.tcaElRefracted > 0.0) {
            elTipItem->setToolTip(
                QString("折射后最大仰角: %1°").arg(p.tcaElRefracted, 0, 'f', 1));
        }
        setCell(6,  QString::number(p.tcaLook.azDeg, 'f', 1) + "°");

        // 可见性
        QString visText;
        QColor visColor;
        switch (p.visibility) {
        case PassVisibility::Visible:   visText = "可见"; visColor = QColor(0, 150, 0); break;
        case PassVisibility::RadioOnly: visText = "无线电"; visColor = QColor(0, 100, 180); break;
        case PassVisibility::Daylight:  visText = "白天"; visColor = QColor(150, 150, 150); break;
        }
        auto *visItem = new QTableWidgetItem(visText);
        visItem->setForeground(visColor);
        m_passTable->setItem(r, 7, visItem);

        setCell(8,  QString::number(p.tcaSunAlt, 'f', 1) + "°");

        // 卫星光照: 阴影状态 + TCA 时刻太阳圆面可见比例（半影带内 1~99%）
        QString litText;
        switch (p.tcaSatShadow) {
        case ShadowStatus::FullSunlight: litText = "日照"; break;
        case ShadowStatus::InPenumbra:
            litText = QString("半影 %1%").arg(qBound(1, static_cast<int>(p.tcaIllum * 100.0), 99));
            break;
        case ShadowStatus::InUmbra:      litText = "本影"; break;
        }
        setCell(9, litText);

        // 可见弧段子区间（Heavens-Above 语义: 该时段内实际可见）
        if (p.hasVisibleArc) {
            auto *arcTip = new QTableWidgetItem(
                jdMsToStr(p.visAosJdMs) + " – " + jdMsToStr(p.visLosJdMs));
            arcTip->setToolTip(
                "几何过境内「暗天 + 被照亮」的连续时段，观测者实际可见范围");
            m_passTable->setItem(r, 10, arcTip);
        } else {
            setCell(10, "—");
        }

        setCell(11, QString("%1°, %2°, %3km")
            .arg(p.aosSatGeo.latDeg, 0, 'f', 1)
            .arg(p.aosSatGeo.lonDeg, 0, 'f', 1)
            .arg(p.aosSatGeo.altKm, 0, 'f', 0));

        setCell(12, QString::number(p.tcaLook.rangeKm, 'f', 0) + " km");
        setCell(13, QString::number(p.dopplerAtTcaKHz, 'f', 2) + " kHz");

        // 星等估算（越小越亮; TCA 时刻）
        if (p.magValid) {
            auto *magItem = new QTableWidgetItem(
                QString::number(p.magAtTca, 'f', 1));
            magItem->setToolTip(
                "视星等估算（Vallerie 相位模型），越小越亮\n"
                "固有星等来自内置目录或输入框回退值，误差 ±1~2 星等");
            m_passTable->setItem(r, 14, magItem);
        } else {
            setCell(14, "—");
        }

        QString qualityText;
        switch (p.quality) {
        case PassQuality::Excellent: qualityText = "★★★"; break;
        case PassQuality::Good:      qualityText = "★★";  break;
        case PassQuality::Poor:      qualityText = "★";   break;
        }
        setCell(15, qualityText);

        // 列对齐: 数值右对齐、状态居中、文本左对齐
        static const Qt::Alignment aligns[16] = {
            Qt::AlignCenter,                   /* # */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 卫星名 */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 入境 */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 出境 */
            Qt::AlignCenter,                   /* 持续 */
            Qt::AlignRight | Qt::AlignVCenter, /* 最大仰角 */
            Qt::AlignRight | Qt::AlignVCenter, /* 方位角 */
            Qt::AlignCenter,                   /* 可见性 */
            Qt::AlignRight | Qt::AlignVCenter, /* 太阳高度 */
            Qt::AlignCenter,                   /* 卫星光照 */
            Qt::AlignCenter,                   /* 可见弧段 */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 入境位置 */
            Qt::AlignRight | Qt::AlignVCenter, /* 斜距 */
            Qt::AlignRight | Qt::AlignVCenter, /* 多普勒 */
            Qt::AlignRight | Qt::AlignVCenter, /* 星等 */
            Qt::AlignCenter                    /* 质量 */
        };
        for (int c = 0; c < 16; ++c) {
            if (auto *it = m_passTable->item(r, c))
                it->setTextAlignment(aligns[c]);
        }

        // 最大仰角颜色
        auto *elItem = m_passTable->item(r, 5);
        if (elItem) {
            if (p.maxElevDeg >= 50.0)      elItem->setForeground(QColor(0, 160, 0));
            else if (p.maxElevDeg >= 20.0) elItem->setForeground(QColor(200, 150, 0));
            else                           elItem->setForeground(QColor(200, 80, 0));
        }
    }
}

// ============================================================================
// 导出
// ============================================================================
void MainWindow::onExportCSV()
{
    if (m_filteredPasses.isEmpty()) return;
    QString filePath = QFileDialog::getSaveFileName(this, "导出 CSV",
        "passes_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd") + ".csv",
        "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "导出失败", "无法写入文件: " + filePath);
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    out.setLocale(QLocale::c());   // 修复: 默认跟随系统 locale, 逗号小数点的
                                   // 区域设置下 CSV 数字会被破坏为 "40,5"

    // 表头
    out << "#,卫星名,入境(UTC),出境(UTC),持续(s),最大仰角(°),折射仰角(°),方位角(°),"
           "可见性,太阳高度(°),卫星光照,可见弧段起点(UTC),可见弧段终点(UTC),"
           "入境纬度(°),入境经度(°),入境高度(km),"
           "斜距(km),多普勒(kHz),星等,质量,AOS光照,TCA光照,LOS光照\n";

    for (int i = 0; i < m_filteredPasses.size(); ++i) {
        const auto &p = m_filteredPasses[i];
        auto jdToStr = [](int64_t jdMs) -> QString {
            qint64 msecs = jdMs - 210866760000000LL;  // JD×86400000 → epoch ms
            return QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC)
                .toString("yyyy-MM-dd HH:mm:ss");
        };
        // 可见性文字
        QString visStr = "白天";
        if (p.visibility == PassVisibility::Visible) visStr = "可见";
        else if (p.visibility == PassVisibility::RadioOnly) visStr = "无线电";
        auto illumStr = [](double f) -> QString {
            if (f <= 0.0)      return "0%";
            if (f >= 1.0)      return "100%";
            return QString("%1%").arg(static_cast<int>(f * 100.0));
        };
        // CSV 字段转义: 卫星名可能含逗号/引号/换行，按 RFC 4180 包裹引号
        auto csvField = [](const QString &s) -> QString {
            return "\"" + QString(s).replace('"', "\"\"") + "\"";
        };
        out << (i + 1) << ","
            << csvField(QString::fromStdString(p.satelliteName)) << ","
            << jdToStr(p.aosJdMs) << ","
            << jdToStr(p.losJdMs) << ","
            << p.durationSec << ","
            << p.maxElevDeg << ","
            << p.tcaElRefracted << ","
            << p.tcaLook.azDeg << ","
            << visStr << ","
            << p.tcaSunAlt << ","
            << ((p.tcaSatShadow == ShadowStatus::FullSunlight) ? "日照" :
                (p.tcaSatShadow == ShadowStatus::InPenumbra) ? "半影" : "本影") << ","
            << (p.hasVisibleArc ? jdToStr(p.visAosJdMs) : "") << ","
            << (p.hasVisibleArc ? jdToStr(p.visLosJdMs) : "") << ","
            << p.aosSatGeo.latDeg << ","
            << p.aosSatGeo.lonDeg << ","
            << p.aosSatGeo.altKm << ","
            << p.tcaLook.rangeKm << ","
            << p.dopplerAtTcaKHz << ","
            << (p.magValid ? QString::number(p.magAtTca, 'f', 1) : "") << ","
            << ((p.quality == PassQuality::Excellent) ? "优秀" :
                (p.quality == PassQuality::Good) ? "良好" : "一般") << ","
            << illumStr(p.aosIllum) << ","
            << illumStr(p.tcaIllum) << ","
            << illumStr(p.losIllum) << "\n";
    }
}

void MainWindow::onExportKML()
{
    if (m_filteredPasses.isEmpty()) return;
    QString filePath = QFileDialog::getSaveFileName(this, "导出 KML",
        "passes_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd") + ".kml",
        "KML Files (*.kml)");
    if (filePath.isEmpty()) return;

    // KML 是 XML：卫星名可能含 & < > 等特殊字符，必须转义
    //（名称来自 TLE 名称行或用户粘贴文本，不可信）
    auto xmlEsc = [](const QString &s) { return s.toHtmlEscaped(); };

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "导出失败", "无法写入文件: " + filePath);
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
        << "<Document>\n"
        << "<name>XGZ Satellite Passes</name>\n";

    // 标注地面站（使用任务锁定坐标, 与图表/轨迹标记一致, 而非当前输入框值）
    out << "<Placemark>\n<name>Ground Station</name>\n"
        << "<Point><coordinates>"
        << m_taskLonDeg << "," << m_taskLatDeg << ",0"
        << "</coordinates></Point>\n</Placemark>\n";

    for (int i = 0; i < m_filteredPasses.size(); ++i) {
        const auto &p = m_filteredPasses[i];
        QString desc = QString("Duration: %1 s; Max elev: %2°")
            .arg(p.durationSec).arg(p.maxElevDeg, 0, 'f', 1);
        if (p.hasVisibleArc) {
            desc += QString("; Visible: %1 – %2")
                .arg(QDateTime::fromMSecsSinceEpoch(p.visAosJdMs - 210866760000000LL,
                                                    QTimeZone::UTC)
                         .toString("HH:mm:ss"))
                .arg(QDateTime::fromMSecsSinceEpoch(p.visLosJdMs - 210866760000000LL,
                                                    QTimeZone::UTC)
                         .toString("HH:mm:ss"));
        }
        if (p.magValid)
            desc += QString("; Mag: %1").arg(p.magAtTca, 0, 'f', 1);
        out << "<Placemark>\n"
            << "<name>Pass " << (i + 1) << ": "
            << xmlEsc(QString::fromStdString(p.satelliteName)) << " "
            << p.maxElevDeg << "°</name>\n"
            << "<description>" << xmlEsc(desc) << "</description>\n"
            << "<LineString><coordinates>";
        // 优先绘制真实采样弧段；无采样时回退 AOS-TCA-LOS 三点
        if (p.samples.size() >= 2) {
            for (const auto &s : p.samples) {
                out << s.geo.lonDeg << "," << s.geo.latDeg << ",0 ";
            }
        } else {
            out << p.aosSatGeo.lonDeg << "," << p.aosSatGeo.latDeg << ",0 ";
            out << p.tcaSatGeo.lonDeg << "," << p.tcaSatGeo.latDeg << ",0 ";
            out << p.losSatGeo.lonDeg << "," << p.losSatGeo.latDeg << ",0 ";
        }
        out << "</coordinates></LineString>\n</Placemark>\n";
    }
    out << "</Document>\n</kml>\n";
}

void MainWindow::onExportJSON()
{
    if (m_filteredPasses.isEmpty()) return;
    QString filePath = QFileDialog::getSaveFileName(this, "导出 JSON",
        "passes_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd") + ".json",
        "JSON Files (*.json)");
    if (filePath.isEmpty()) return;

    QJsonArray arr;
    for (const auto &p : m_filteredPasses) {
        QJsonObject obj;
        obj["satellite"]   = QString::fromStdString(p.satelliteName);
        obj["aosJdMs"]     = static_cast<qint64>(p.aosJdMs);
        obj["losJdMs"]     = static_cast<qint64>(p.losJdMs);
        obj["tcaJdMs"]     = static_cast<qint64>(p.tcaJdMs);
        obj["maxElevDeg"]  = p.maxElevDeg;
        obj["durationSec"] = p.durationSec;
        obj["visibility"]  = static_cast<int>(p.visibility);
        obj["quality"]     = static_cast<int>(p.quality);
        obj["dopplerKHz"]  = p.dopplerAtTcaKHz;
        obj["aosSunAlt"]   = p.aosSunAlt;
        obj["aosSatLit"]   = static_cast<int>(p.aosSatShadow);
        obj["aosIllum"]    = p.aosIllum;
        obj["tcaIllum"]    = p.tcaIllum;
        obj["losIllum"]    = p.losIllum;
        obj["aosLat"]      = p.aosSatGeo.latDeg;
        obj["aosLon"]      = p.aosSatGeo.lonDeg;
        obj["aosAltKm"]    = p.aosSatGeo.altKm;
        obj["aosAzDeg"]    = p.aosLook.azDeg;
        obj["aosRangeKm"]  = p.aosLook.rangeKm;
        obj["hasVisibleArc"] = p.hasVisibleArc;
        if (p.hasVisibleArc) {
            obj["visAosJdMs"] = static_cast<qint64>(p.visAosJdMs);
            obj["visLosJdMs"] = static_cast<qint64>(p.visLosJdMs);
        }
        obj["magValid"] = p.magValid;
        if (p.magValid) obj["magAtTca"] = p.magAtTca;
        arr.append(obj);
    }
    QJsonDocument doc(arr);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "导出失败", "无法写入文件: " + filePath);
        return;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
}

// ============================================================================
// 构造/析构
// ============================================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUI();
    setupConnections();
}

MainWindow::~MainWindow()
{
    // 服务对象由 Qt 父子关系自动管理，无需显式调用
}

#include "MainWindow.moc"
