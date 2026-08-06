// =============================================================================
// MainWindow.cpp — 主窗口完整实现
// =============================================================================
#include "MainWindow.h"
#include "PolarPlotWidget.h"
#include "GroundTrackWidget.h"
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

// 一颗卫星的原始文本块
struct TleBlock {
    QString name;    // 名称行（可能为空）
    QString line1;
    QString line2;
};

// 解析多颗 TLE 文本块: 名称行(可选) + 行1 + 行2
QVector<TleBlock> parseTleBlocks(const QString &text)
{
    QVector<TleBlock> blocks;
    QString pendingName;
    QString pendingL1;
    const QStringList lines = text.split('\n');
    for (const QString &raw : lines) {
        const QString l = raw.trimmed();
        if (l.isEmpty()) continue;
        if (l.startsWith("1 ") || l.startsWith('1')) {
            pendingL1 = l;
        } else if (l.startsWith("2 ") || l.startsWith('2')) {
            if (!pendingL1.isEmpty()) {
                blocks.append({ pendingName, pendingL1, l });
                pendingName.clear();
                pendingL1.clear();
            }
        } else {
            pendingName = l;   // 非 TLE 行视为下一颗卫星的名称
        }
    }
    return blocks;
}

} // namespace

// ============================================================================
// 构建 UI
// ============================================================================
void MainWindow::setupUI()
{
    setWindowTitle("XGZ — 卫星 TLE 过境预报系统");
    resize(1280, 800);

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
    m_statusModel = new QLabel("模型: —");
    m_statusEpoch = new QLabel("历元: —");
    m_statusAge   = new QLabel("龄期: —");
    m_statusCount = new QLabel("过境: 0");
    statusLayout->addWidget(m_statusModel);
    statusLayout->addWidget(m_statusEpoch);
    statusLayout->addWidget(m_statusAge);
    statusLayout->addWidget(m_statusCount);
    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);

    // ---- 结果区（三页签） ----
    m_tabWidget = new QTabWidget;

    // 页签 1: 过境列表
    m_passTable = new QTableWidget;
    m_passTable->setColumnCount(14);
    m_passTable->setHorizontalHeaderLabels({
        "#", "卫星名", "入境(UTC)", "出境(UTC)", "持续",
        "最大仰角", "方位角", "可见性", "太阳高度", "卫星光照",
        "入境位置", "斜距", "多普勒", "质量"
    });
    m_passTable->horizontalHeader()->setStretchLastSection(true);
    m_passTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_passTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_passTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_passTable->setAlternatingRowColors(true);
    m_passTable->verticalHeader()->setVisible(false);
    m_tabWidget->addTab(m_passTable, "过境列表");

    // 页签 2: 极坐标图
    m_polarPlot = new PolarPlotWidget;
    m_tabWidget->addTab(m_polarPlot, "极坐标图");

    // 页签 3: 地面轨迹图
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
    connect(m_passTable, &QTableWidget::cellClicked,
            this, &MainWindow::onTableRowSelected);
    // 切换"仅显示可见过境"时立即重新过滤并刷新表格/图表
    connect(m_visibleOnlyCheck, &QCheckBox::toggled, this, [this](bool) {
        if (m_currentPasses.isEmpty()) return;
        if (m_visibleOnlyCheck->isChecked()) {
            QVector<PassEvent> filtered;
            for (const auto &p : m_currentPasses) {
                if (p.visibility == PassVisibility::Visible)
                    filtered.append(p);
            }
            m_filteredPasses = filtered;
        } else {
            m_filteredPasses = m_currentPasses;
        }
        populateTable(m_filteredPasses);
        m_polarPlot->setPasses(m_filteredPasses);
        m_groundTrack->setPasses(m_filteredPasses, m_latSpin->value(), m_lonSpin->value());
    });
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

    m_batchMode = false;
    m_batchErrors.clear();
    m_predictBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0);  // 不确定进度

    m_service->startJob(job);
}

// ============================================================================
// 批量导入
// ============================================================================
void MainWindow::onBatchImport()
{
    BatchTleDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto blocks = parseTleBlocks(dlg.text());
    if (blocks.isEmpty()) {
        QMessageBox::warning(this, "批量导入", "未识别到有效的 TLE 两行根数");
        return;
    }

    QVector<PassPredictWorker::Job> jobs;
    int failed = 0;
    for (const auto &b : blocks) {
        TleParser::ParseError parseErr;
        auto tleOpt = TleParser::parse(b.line1.toStdString(), b.line2.toStdString(),
                                       &parseErr);
        if (!tleOpt) {
            ++failed;
            continue;
        }
        PassPredictWorker::Job job;
        fillJobFromUi(job);
        job.tleLine0 = b.name;
        job.tleLine1 = b.line1;
        job.tleLine2 = b.line2;
        jobs.append(job);
    }

    if (jobs.isEmpty()) {
        QMessageBox::warning(this, "批量导入",
            QString("共 %1 颗卫星，全部解析失败，请检查 TLE 内容")
                .arg(blocks.size()));
        return;
    }

    m_batchMode = true;
    m_batchErrors.clear();
    m_predictBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, jobs.size());
    m_progressBar->setValue(0);
    m_statusModel->setText(QString("模型: 批量 %1 颗").arg(jobs.size()));
    if (failed > 0)
        m_statusModel->setText(
            QString("模型: 批量 %1 颗（%2 颗解析失败跳过）")
                .arg(jobs.size()).arg(failed));

    m_service->startBatch(jobs);
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
    m_statusModel->setText(QString("模型: 正在下载 %1 的 TLE…")
                           .arg(m_tleGroupCombo->currentText()));
    m_tleFetcher->fetchGroup(groupId);
}

// ============================================================================
// TLE 下载完成 → 解析并弹出批量导入对话框（预填文本）
// ============================================================================
void MainWindow::onTleDownloaded(QString rawText, QString groupId)
{
    const auto blocks = parseTleBlocks(rawText);
    if (blocks.isEmpty()) {
        QMessageBox::warning(this, "在线更新",
            QString("组 [%1] 未解析到有效 TLE（可能是空数据或格式变化）")
                .arg(groupId));
        return;
    }
    m_statusModel->setText(QString("模型: 已下载 %1 颗卫星的 TLE").arg(blocks.size()));

    BatchTleDialog dlg(this);
    dlg.setWindowTitle(QString("批量导入 — 在线下载 (%1 颗)").arg(blocks.size()));
    dlg.setText(rawText);
    if (dlg.exec() != QDialog::Accepted) return;

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
        QMessageBox::warning(this, "在线更新",
            QString("共 %1 颗卫星，全部解析失败，请检查网络数据")
                .arg(blocks.size()));
        return;
    }

    m_batchMode = true;
    m_batchErrors.clear();
    m_predictBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, jobs.size());
    m_progressBar->setValue(0);
    m_statusModel->setText(QString("模型: 批量 %1 颗（在线）").arg(jobs.size()));
    if (failed > 0)
        m_statusModel->setText(
            QString("模型: 批量 %1 颗（%2 颗解析失败跳过）")
                .arg(jobs.size()).arg(failed));

    m_service->startBatch(jobs);
}

void MainWindow::onTleError(QString message)
{
    m_statusModel->setText("模型: TLE 下载失败");
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
    job.calcCfg.coarseStepSec      = 60.0;
    job.calcCfg.bisectionIters     = 18;
    job.calcCfg.enableSunAnalysis  = true;
    job.calcCfg.enableShadowCheck  = true;
    job.calcCfg.enableRefraction   = true;
    job.calcCfg.enableDoppler      = true;
    job.calcCfg.carrierFreqMHz     = m_freqSpin->value();
    job.calcCfg.darkSkyThresholdDeg = m_darkSkyCombo->currentData().toDouble();
}

void MainWindow::onCancel()
{
    m_service->cancelAll();
    m_batchMode = false;
    m_batchErrors.clear();
    m_predictBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
}

void MainWindow::onClear()
{
    m_tleInput1->clear();
    m_tleInput2->clear();
    m_passTable->setRowCount(0);
    m_currentPasses.clear();
    m_polarPlot->clear();
    m_groundTrack->clear();
    m_statusModel->setText("模型: —");
    m_statusEpoch->setText("历元: —");
    m_statusAge->setText("龄期: —");
    m_statusCount->setText("过境: 0");
}

// ============================================================================
// 结果处理
// ============================================================================
void MainWindow::onResultsReady(QVector<PassEvent> passes)
{
    m_currentPasses = passes;

    // 过滤可见过境（过滤后结果替换表格和可视化数据）
    if (m_visibleOnlyCheck->isChecked()) {
        QVector<PassEvent> filtered;
        for (const auto &p : passes) {
            if (p.visibility == PassVisibility::Visible)
                filtered.append(p);
        }
        m_filteredPasses = filtered;  // 导出使用过滤后数据
    } else {
        m_filteredPasses = passes;
    }

    populateTable(m_filteredPasses);
    m_polarPlot->setPasses(m_filteredPasses);
    m_groundTrack->setPasses(m_filteredPasses, m_latSpin->value(), m_lonSpin->value());

    // 统计用原始数据
    int visCount = std::count_if(m_currentPasses.begin(), m_currentPasses.end(),
        [](const PassEvent &p) { return p.visibility == PassVisibility::Visible; });
    m_statusCount->setText(QString("过境: %1 (可见: %2)")
        .arg(m_currentPasses.size()).arg(visCount));

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
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
}

void MainWindow::onError(QString msg)
{
    if (m_batchMode) {
        // 批量模式: 不弹阻塞对话框，累计到结果时统一汇总
        m_batchErrors << msg;
        m_statusModel->setText(QString("模型: 批量错误 %1 项").arg(m_batchErrors.size()));
        return;
    }
    QMessageBox::warning(this, "错误", msg);
    m_predictBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
}

void MainWindow::onTableRowSelected(int row, int col)
{
    Q_UNUSED(col);
    if (row >= 0 && row < m_passTable->rowCount()) {
        m_polarPlot->setSelectedPass(row);
        m_groundTrack->setSelectedPass(row);
    }
}

// ============================================================================
// 表格填充
// ============================================================================
void MainWindow::populateTable(const QVector<PassEvent> &passes)
{
    m_passTable->setRowCount(0);
    m_passTable->setRowCount(passes.size());

    int tzOffsetSec = m_timezoneCombo->currentData().toInt();
    QTimeZone displayTz = (tzOffsetSec == 0)
        ? QTimeZone::UTC
        : QTimeZone(tzOffsetSec);

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
        setCell(4,  QString("%1s").arg(static_cast<int>(p.durationSec)));
        setCell(5,  QString::number(p.maxElevDeg, 'f', 1) + "°");
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

        setCell(10, QString("%1°, %2°, %3km")
            .arg(p.aosSatGeo.latDeg, 0, 'f', 1)
            .arg(p.aosSatGeo.lonDeg, 0, 'f', 1)
            .arg(p.aosSatGeo.altKm, 0, 'f', 0));

        setCell(11, QString::number(p.tcaLook.rangeKm, 'f', 0) + " km");
        setCell(12, QString::number(p.dopplerAtTcaKHz, 'f', 2) + " kHz");

        QString qualityText;
        switch (p.quality) {
        case PassQuality::Excellent: qualityText = "★★★"; break;
        case PassQuality::Good:      qualityText = "★★";  break;
        case PassQuality::Poor:      qualityText = "★";   break;
        }
        setCell(13, qualityText);

        // 最大仰角颜色
        auto *elItem = m_passTable->item(r, 5);
        if (elItem) {
            if (p.maxElevDeg >= 50.0)      elItem->setForeground(QColor(0, 160, 0));
            else if (p.maxElevDeg >= 20.0) elItem->setForeground(QColor(200, 150, 0));
            else                           elItem->setForeground(QColor(200, 80, 0));
        }
    }

    m_passTable->resizeColumnsToContents();
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

    // 表头
    out << "#,卫星名,入境(UTC),出境(UTC),持续(s),最大仰角(°),方位角(°),"
           "可见性,太阳高度(°),卫星光照,入境纬度(°),入境经度(°),入境高度(km),"
           "斜距(km),多普勒(kHz),质量,AOS光照,TCA光照,LOS光照\n";

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
        out << (i + 1) << ","
            << QString::fromStdString(p.satelliteName) << ","
            << jdToStr(p.aosJdMs) << ","
            << jdToStr(p.losJdMs) << ","
            << p.durationSec << ","
            << p.maxElevDeg << ","
            << p.tcaLook.azDeg << ","
            << visStr << ","
            << p.tcaSunAlt << ","
            << ((p.tcaSatShadow == ShadowStatus::FullSunlight) ? "日照" :
                (p.tcaSatShadow == ShadowStatus::InPenumbra) ? "半影" : "本影") << ","
            << p.aosSatGeo.latDeg << ","
            << p.aosSatGeo.lonDeg << ","
            << p.aosSatGeo.altKm << ","
            << p.tcaLook.rangeKm << ","
            << p.dopplerAtTcaKHz << ","
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

    // 标注地面站
    out << "<Placemark>\n<name>Ground Station</name>\n"
        << "<Point><coordinates>"
        << m_lonSpin->value() << "," << m_latSpin->value() << ",0"
        << "</coordinates></Point>\n</Placemark>\n";

    for (int i = 0; i < m_filteredPasses.size(); ++i) {
        const auto &p = m_filteredPasses[i];
        out << "<Placemark>\n"
            << "<name>Pass " << (i + 1) << ": "
            << QString::fromStdString(p.satelliteName) << " "
            << p.maxElevDeg << "°</name>\n"
            << "<description>Duration: " << p.durationSec << "s</description>\n"
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
