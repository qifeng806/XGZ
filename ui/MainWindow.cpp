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
#include "core/ErrorHandler.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QDialog>
#include <QFile>
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
#include <QtConcurrent>   // 导出文件写入异步化（Qt6 中 QtConcurrent 是独立模块, XGZ.pro 已 QT += concurrent）
#include <QFutureWatcher>
#include <cmath>          // std::abs（KML 反经线跨越断线）
#include <vector>

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

// ============================================================================
// 导出文件写入器（QtConcurrent 线程池执行; 输入为共享过境指针集,
// 只读解引用——导出期间所有数据变更入口被 startExport 锁定, 指针必然有效;
// 返回错误串, 空串=成功; 不触碰任何 UI 成员, 无跨线程数据竞争）
// ============================================================================
QString writePassesCsv(const QVector<const PassEvent *> &passes, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("无法写入文件: %1").arg(path);
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

    auto jdToStr = [](int64_t jdMs) -> QString {
        qint64 msecs = jdMs - 210866760000000LL;  // JD×86400000 → epoch ms
        return QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC)
            .toString("yyyy-MM-dd HH:mm:ss");
    };
    auto illumStr = [](double f) -> QString {
        if (f <= 0.0)      return "0%";
        if (f >= 1.0)      return "100%";
        return QString("%1%").arg(static_cast<int>(f * 100.0));
    };
    // CSV 字段转义: 卫星名可能含逗号/引号/换行，按 RFC 4180 包裹引号
    auto csvField = [](const QString &s) -> QString {
        return "\"" + QString(s).replace('"', "\"\"") + "\"";
    };

    for (int i = 0; i < passes.size(); ++i) {
        const PassEvent *p = passes[i];
        // 可见性文字
        QString visStr = "白天";
        if (p->visibility == PassVisibility::Visible) visStr = "可见";
        else if (p->visibility == PassVisibility::RadioOnly) visStr = "无线电";
        out << (i + 1) << ","
            << csvField(QString::fromStdString(p->satelliteName)) << ","
            << jdToStr(p->aosJdMs) << ","
            << jdToStr(p->losJdMs) << ","
            << p->durationSec << ","
            << p->maxElevDeg << ","
            << p->tcaElRefracted << ","
            << p->tcaLook.azDeg << ","
            << visStr << ","
            << p->tcaSunAlt << ","
            << ((p->tcaSatShadow == ShadowStatus::FullSunlight) ? "日照" :
                (p->tcaSatShadow == ShadowStatus::InPenumbra) ? "半影" : "本影") << ","
            << (p->hasVisibleArc ? jdToStr(p->visAosJdMs) : "") << ","
            << (p->hasVisibleArc ? jdToStr(p->visLosJdMs) : "") << ","
            << p->aosSatGeo.latDeg << ","
            << p->aosSatGeo.lonDeg << ","
            << p->aosSatGeo.altKm << ","
            << p->tcaLook.rangeKm << ","
            << p->dopplerAtTcaKHz << ","
            << (p->magValid ? QString::number(p->magAtTca, 'f', 1) : "") << ","
            << ((p->quality == PassQuality::Excellent) ? "优秀" :
                (p->quality == PassQuality::Good) ? "良好" : "一般") << ","
            << illumStr(p->aosIllum) << ","
            << illumStr(p->tcaIllum) << ","
            << illumStr(p->losIllum) << "\n";
    }
    // 写盘结果校验: 磁盘满/IO 错误时缓冲可能未落盘, 静默返回成功会生成
    // 空或半截文件且 UI 报"导出成功"
    out.flush();
    if (out.status() != QTextStream::Ok || file.error() != QFileDevice::NoError)
        return QStringLiteral("CSV 写入失败（磁盘空间不足或 IO 错误）: %1").arg(path);
    return QString();
}

QString writePassesKml(const QVector<const PassEvent *> &passes, const QString &path,
                       double taskLatDeg, double taskLonDeg)
{
    // KML 是 XML：卫星名可能含 & < > 等特殊字符，必须转义
    //（名称来自 TLE 名称行或用户粘贴文本，不可信）
    auto xmlEsc = [](const QString &s) { return s.toHtmlEscaped(); };

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("无法写入文件: %1").arg(path);
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
        << "<Document>\n"
        << "<name>XGZ Satellite Passes</name>\n";

    // 标注地面站（使用任务锁定坐标, 与图表/轨迹标记一致, 而非当前输入框值）
    out << "<Placemark>\n<name>Ground Station</name>\n"
        << "<Point><coordinates>"
        << taskLonDeg << "," << taskLatDeg << ",0"
        << "</coordinates></Point>\n</Placemark>\n";

    for (int i = 0; i < passes.size(); ++i) {
        const PassEvent *p = passes[i];
        QString desc = QString("Duration: %1 s; Max elev: %2°")
            .arg(p->durationSec).arg(p->maxElevDeg, 0, 'f', 1);
        if (p->hasVisibleArc) {
            desc += QString("; Visible: %1 – %2")
                .arg(QDateTime::fromMSecsSinceEpoch(p->visAosJdMs - 210866760000000LL,
                                                    QTimeZone::UTC)
                         .toString("HH:mm:ss"))
                .arg(QDateTime::fromMSecsSinceEpoch(p->visLosJdMs - 210866760000000LL,
                                                    QTimeZone::UTC)
                         .toString("HH:mm:ss"));
        }
        if (p->magValid)
            desc += QString("; Mag: %1").arg(p->magAtTca, 0, 'f', 1);
        out << "<Placemark>\n"
            << "<name>Pass " << (i + 1) << ": "
            << xmlEsc(QString::fromStdString(p->satelliteName)) << " "
            << p->maxElevDeg << "°</name>\n"
            << "<description>" << xmlEsc(desc) << "</description>\n";
        // 优先绘制真实采样弧段；无采样时回退 AOS-TCA-LOS 三点。
        // ±180° 反经线跨越断线: 相邻点经度差 >180° 时 KML 会画成横穿
        // 整幅地图的长直线伪影——按跨越点切分为多个 LineString
        //（与 GroundTrackWidget/PolarPlotWidget 的断线策略一致）
        QVector<QVector<QPair<double, double>>> segs;
        segs.append(QVector<QPair<double, double>>());
        auto appendPt = [&segs](double lon, double lat) {
            auto &cur = segs.last();
            if (!cur.isEmpty()) {
                const double prevLon = cur.last().first;
                if (std::abs(lon - prevLon) > 180.0)
                    segs.append(QVector<QPair<double, double>>());   // 跨反经线: 断开新段
            }
            cur.append(qMakePair(lon, lat));
        };
        if (p->samples.size() >= 2) {
            for (const auto &s : p->samples)
                appendPt(s.geo.lonDeg, s.geo.latDeg);
        } else {
            appendPt(p->aosSatGeo.lonDeg, p->aosSatGeo.latDeg);
            appendPt(p->tcaSatGeo.lonDeg, p->tcaSatGeo.latDeg);
            appendPt(p->losSatGeo.lonDeg, p->losSatGeo.latDeg);
        }
        for (const auto &seg : segs) {
            if (seg.size() < 2) continue;   // 单点无法成线
            out << "<LineString><coordinates>";
            for (const auto &pt : seg)
                out << pt.first << "," << pt.second << ",0 ";
            out << "</coordinates></LineString>\n";
        }
        out << "</Placemark>\n";
    }
    out << "</Document>\n</kml>\n";
    // 写盘结果校验（同 CSV）: 磁盘满/IO 错误不得静默报成功
    out.flush();
    if (out.status() != QTextStream::Ok || file.error() != QFileDevice::NoError)
        return QStringLiteral("KML 写入失败（磁盘空间不足或 IO 错误）: %1").arg(path);
    return QString();
}

QString writePassesJson(const QVector<const PassEvent *> &passes, const QString &path)
{
    QJsonArray arr;
    for (const PassEvent *p : passes) {
        QJsonObject obj;
        obj["satellite"]   = QString::fromStdString(p->satelliteName);
        obj["aosJdMs"]     = static_cast<qint64>(p->aosJdMs);
        obj["losJdMs"]     = static_cast<qint64>(p->losJdMs);
        obj["tcaJdMs"]     = static_cast<qint64>(p->tcaJdMs);
        obj["maxElevDeg"]  = p->maxElevDeg;
        obj["durationSec"] = p->durationSec;
        obj["visibility"]  = static_cast<int>(p->visibility);
        obj["quality"]     = static_cast<int>(p->quality);
        obj["dopplerKHz"]  = p->dopplerAtTcaKHz;
        obj["aosSunAlt"]   = p->aosSunAlt;
        obj["aosSatLit"]   = static_cast<int>(p->aosSatShadow);
        obj["aosIllum"]    = p->aosIllum;
        obj["tcaIllum"]    = p->tcaIllum;
        obj["losIllum"]    = p->losIllum;
        obj["aosLat"]      = p->aosSatGeo.latDeg;
        obj["aosLon"]      = p->aosSatGeo.lonDeg;
        obj["aosAltKm"]    = p->aosSatGeo.altKm;
        obj["aosAzDeg"]    = p->aosLook.azDeg;
        obj["aosRangeKm"]  = p->aosLook.rangeKm;
        obj["hasVisibleArc"] = p->hasVisibleArc;
        if (p->hasVisibleArc) {
            obj["visAosJdMs"] = static_cast<qint64>(p->visAosJdMs);
            obj["visLosJdMs"] = static_cast<qint64>(p->visLosJdMs);
        }
        obj["magValid"] = p->magValid;
        if (p->magValid) obj["magAtTca"] = p->magAtTca;
        arr.append(obj);
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("无法写入文件: %1").arg(path);
    if (file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented)) < 0
        || file.error() != QFileDevice::NoError)
        return QStringLiteral("JSON 写入失败（磁盘空间不足或 IO 错误）: %1").arg(path);
    return QString();
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

    // 页签 1: 过境列表（QTableView + 懒加载 model: 仅可见单元格被请求,
    // 批量过境不再整表构建 QTableWidgetItem——M 修复的刷新卡顿根源）
    m_tableModel = new PassTableModel(this);
    m_passTable = new QTableView;
    m_passTable->setModel(m_tableModel);
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
// 断开所有信号槽连接
// ============================================================================
void MainWindow::disconnectAllConnections()
{
    // 断开所有按钮的信号槽连接
    disconnect(m_predictBtn, &QPushButton::clicked, this, &MainWindow::onPredict);
    disconnect(m_batchImportBtn, &QPushButton::clicked, this, &MainWindow::onBatchImport);
    disconnect(m_onlineUpdateBtn, &QPushButton::clicked, this, &MainWindow::onOnlineUpdate);
    disconnect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancel);
    disconnect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClear);
    disconnect(m_exportCsvBtn, &QPushButton::clicked, this, &MainWindow::onExportCSV);
    disconnect(m_exportKmlBtn, &QPushButton::clicked, this, &MainWindow::onExportKML);
    disconnect(m_exportJsonBtn, &QPushButton::clicked, this, &MainWindow::onExportJSON);
    
    // 断开TleFetcher的信号槽连接
    if (m_tleFetcher) {
        disconnect(m_tleFetcher.get(), &TleFetcher::downloaded, this, &MainWindow::onTleDownloaded);
        disconnect(m_tleFetcher.get(), &TleFetcher::fetchError, this, &MainWindow::onTleError);
        disconnect(m_tleFetcher.get(), &TleFetcher::finished, this, nullptr);
    }
    
    // 断开PassPredictorService的信号槽连接
    if (m_service) {
        disconnect(m_service.get(), &PassPredictorService::resultReady, this, &MainWindow::onResultsReady);
        disconnect(m_service.get(), &PassPredictorService::errorOccurred, this, &MainWindow::onError);
        disconnect(m_service.get(), &PassPredictorService::checksumWarning, this, &MainWindow::onChecksumWarning);
        disconnect(m_service.get(), &PassPredictorService::batchProgress, this, nullptr);
        disconnect(m_service.get(), &PassPredictorService::metaInfo, this, &MainWindow::onMetaInfo);
    }
    
    // 断开表格选择模型的信号槽连接
    if (m_passTable && m_passTable->selectionModel()) {
        disconnect(m_passTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this, nullptr);
    }
    
    // 断开其他UI控件的信号槽连接
    disconnect(m_visibleOnlyCheck, &QCheckBox::toggled, this, nullptr);
    disconnect(m_timezoneCombo, &QComboBox::currentIndexChanged, this, nullptr);
    
    // 断开导出watcher的信号槽连接
    if (m_exportWatcher) {
        disconnect(m_exportWatcher.get(), &QFutureWatcher<QString>::finished, this, &MainWindow::onExportFinished);
    }
}

// ============================================================================
// 槽函数连接
// ============================================================================
void MainWindow::setupConnections()
{
    m_service = std::make_unique<PassPredictorService>(this);
    m_tleFetcher = std::make_unique<TleFetcher>(this);

    connect(m_predictBtn, &QPushButton::clicked, this, &MainWindow::onPredict);
    connect(m_batchImportBtn, &QPushButton::clicked, this, &MainWindow::onBatchImport);
    connect(m_onlineUpdateBtn, &QPushButton::clicked, this, &MainWindow::onOnlineUpdate);
    connect(m_tleFetcher.get(), &TleFetcher::downloaded, this, &MainWindow::onTleDownloaded);
    connect(m_tleFetcher.get(), &TleFetcher::fetchError, this, &MainWindow::onTleError);
    connect(m_tleFetcher.get(), &TleFetcher::finished, this, [this]() {
        // 检查对象是否仍然有效
        if (!this->isVisible()) return;

        // 批量态（含后台解析窗口期——m_batchMode 已由 startBatchFromBlocks
        // 提前置位）: 入口保持禁用, 收尾由批量完成/失败路径统一负责。
        // 此前无条件恢复会在解析窗口内越权启用预报/导出, 用户点击的
        // 任务随后被 startBatch→cleanupAll 静默替换
        if (m_batchMode) return;

        m_onlineUpdateBtn->setEnabled(true);
        m_onlineUpdateBtn->setText("下载 TLE");
        setExportEnabled(true);   // 下载结束, 导出入口恢复
        m_predictBtn->setEnabled(true);
        m_batchImportBtn->setEnabled(true);
        m_cancelBtn->setEnabled(false);
    });
    connect(m_cancelBtn,  &QPushButton::clicked, this, &MainWindow::onCancel);
    connect(m_clearBtn,   &QPushButton::clicked, this, &MainWindow::onClear);
    connect(m_exportCsvBtn, &QPushButton::clicked, this, &MainWindow::onExportCSV);
    connect(m_exportKmlBtn, &QPushButton::clicked, this, &MainWindow::onExportKML);
    connect(m_exportJsonBtn, &QPushButton::clicked, this, &MainWindow::onExportJSON);

    connect(m_service.get(), &PassPredictorService::resultReady,
            this, &MainWindow::onResultsReady);
    connect(m_service.get(), &PassPredictorService::errorOccurred,
            this, &MainWindow::onError);
    // 校验和警告独立通道: 仅状态栏去重提示——不入失败汇总、不弹窗、
    // 不复位控件（计算仍在进行, resultReady 随后正常到达）
    connect(m_service.get(), &PassPredictorService::checksumWarning,
            this, &MainWindow::onChecksumWarning);
    connect(m_service.get(), &PassPredictorService::batchProgress,
            this, [this](int completed, int total) {
                // 检查对象是否仍然有效
                if (!this->isVisible()) return;
                
                m_progressBar->setRange(0, total);
                m_progressBar->setValue(completed);
            });
    connect(m_service.get(), &PassPredictorService::metaInfo,
            this, &MainWindow::onMetaInfo);
    // 点击/键盘方向键/程序化移动统一走 currentRowChanged（model 视图方案）
    connect(m_passTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &) {
                // 检查对象是否仍然有效
                if (!this->isVisible()) return;
                
                if (cur.isValid()) onTableRowSelected(cur.row(), 0);
            });
    // 切换"仅显示可见过境"时立即重新过滤并刷新表格/图表
    connect(m_visibleOnlyCheck, &QCheckBox::toggled, this, [this](bool) {
        // 检查对象是否仍然有效
        if (!this->isVisible()) return;
        
        if (m_currentPasses.isEmpty()) return;
        applyFilterAndRefresh();
    });
    // 时区切换: 无需重新计算, 重建表格时间列与表头即可（H3 修复）
    connect(m_timezoneCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        // 检查对象是否仍然有效
        if (!this->isVisible()) return;
        
        if (m_currentPasses.isEmpty()) return;
        applyFilterAndRefresh();
    });
}

// ============================================================================
// 按可见性开关过滤并刷新表格/图表/统计
// ============================================================================
void MainWindow::applyFilterAndRefresh()
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    // 增加UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(1);
    
    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 创建过滤后的数据副本
    QVector<const PassEvent *> filtered;
    if (m_visibleOnlyCheck->isChecked()) {
        for (const auto &p : m_currentPasses)
            if (p.visibility == PassVisibility::Visible)
                filtered.append(&p);
    } else {
        filtered.reserve(m_currentPasses.size());
        for (const auto &p : m_currentPasses)
            filtered.append(&p);
    }
    
    // 保存当前选中卫星身份与滚动位置: 过滤/时区切换后行序收缩,
    // 按行号恢复会漂移到另一颗卫星（越界钳到第 0 行同样漂移）,
    // 必须以「卫星 + 过境时刻」身份键找回原选择
    const int prevRow = (m_tableModel->rowCount() > 0)
        ? m_passTable->currentIndex().row() : -1;
    QString prevKey;
    if (prevRow >= 0 && prevRow < m_filteredPasses.size()) {
        const auto *prev = m_filteredPasses[prevRow];
        prevKey = QString::number(prev->aosJdMs) + "|" +
                  QString::fromStdString(prev->satelliteName);
    }
    const int prevScroll = m_passTable->verticalScrollBar()->value();
    
    // 更新过滤后的数据
    m_filteredPasses = filtered;
    
    // 释放数据锁，允许UI更新
    dataLocker.unlock();
    
    // 刷新UI（在主线程中执行）
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

    // 恢复选中: 按身份键找回原选择（行序可能已变）, 找不到/无选中时钳到第 0 行
    if (m_tableModel->rowCount() > 0) {
        int row = 0;
        if (!prevKey.isEmpty()) {
            for (int i = 0; i < m_filteredPasses.size(); ++i) {
                const auto *p = m_filteredPasses[i];
                const QString key = QString::number(p->aosJdMs) + "|" +
                                    QString::fromStdString(p->satelliteName);
                if (key == prevKey) { row = i; break; }
            }
        }
        m_passTable->selectRow(row);
    }
    // 恢复滚动位置（模型重置后滚动条归零; 超界值由视图自动钳制）
    m_passTable->verticalScrollBar()->setValue(prevScroll);
    
    // 减少UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(-1);
}

void MainWindow::setStatusText(QLabel *label, const QString &text)
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    label->setText(text);
    label->setToolTip(text);   // 文本超长时悬停查看完整内容
}

void MainWindow::showStatusDedup(const QString &text)
{
    // 连续相同文本只刷一次: 批量失败风暴（千颗同因失败）下错误/恢复信号
    // 逐条到达, 逐条重绘占满 UI 线程; 首条已表达全部信息, 后续相同条目丢弃
    if (m_lastErrorStatusText == text) return;
    m_lastErrorStatusText = text;
    setStatusText(m_statusModel, text);
}

// ============================================================================
// 预报触发
// ============================================================================
void MainWindow::onPredict()
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
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
    m_lastErrorStatusText.clear();   // 新任务开始: 错误状态栏去重复位
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    setExportEnabled(false);   // 计算期间导出旧数据无意义, 一并禁用
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
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
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
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    const QString groupId = m_tleGroupCombo->currentData().toString();
    if (groupId.isEmpty()) return;
    m_onlineUpdateBtn->setEnabled(false);
    m_onlineUpdateBtn->setText("下载中…");
    // 下载期间禁止发起预报/批量/导出（防止任务与下载交错, 导出需稳定数据）
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    setExportEnabled(false);
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
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
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

    // 与"批量导入"入口语义统一: 以对话框编辑后的文本为唯一数据源。
    // 用户可能删除/增补/修改卫星, 预解析的 blocks 在此作废重解析
    int editedIncomplete = 0;
    const auto editedBlocks = parseTleBlocks(dlg.text(), &editedIncomplete);
    if (editedBlocks.isEmpty()) {
        QMessageBox::warning(this, "在线更新",
            "编辑后的文本中未识别到有效的 TLE 两行根数");
        return;
    }
    if (editedIncomplete > 0) {
        QMessageBox::warning(this, "在线更新",
            QString("编辑后的文本中有 %2 条 TLE 不完整，已跳过（其余 %1 颗正常）。")
                .arg(editedBlocks.size()).arg(editedIncomplete));
    }
    startBatchFromBlocks(editedBlocks, "在线更新");
}

// ============================================================================
// 公共批量启动: 解析校验 TLE 块 → 构建任务 → 启动批量预报
// 供"批量导入"与"在线更新下载"两条入口复用
// ============================================================================
void MainWindow::startBatchFromBlocks(const QVector<TleBlock> &blocks,
                                      const QString &origin)
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;

    // 解析期即禁用入口: 千颗级后台解析有数百毫秒窗口, 期间重复触发
    // 会并发两个批次（seq 守卫保证结果不串, 但浪费计算且 UX 混乱）。
    // 全部失败时由 startBatchParsed 失败分支恢复
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    setExportEnabled(false);

    // 提前置位批量态: TleFetcher 的 finished 在 downloaded 同步链返回后才
    // 发射——若等异步解析回调才置位, finished 的 !m_batchMode 守卫会在
    // 解析窗口期越权恢复预报/导出入口, 用户随后点击的任务会被
    // startBatch→cleanupAll 静默替换（凭空消失）
    m_batchMode = true;
    m_batchParsePending = true;

    // 批量解析在 QtConcurrent 线程池执行（TleParser 为纯函数, 无 UI 访问）:
    // 千颗级粘贴/下载在 GUI 线程串行解析会冻结界面数百毫秒。
    // UI 参数先快照为模板 job（后台线程禁止读取 UI 控件）
    PassPredictWorker::Job templateJob;
    fillJobFromUi(templateJob);

    struct ParseResult {
        QVector<PassPredictWorker::Job> jobs;
        int failed = 0;
        int lenIssues = 0;   // TLE 行长度不足计数（错误诊断用）
        int fmtIssues = 0;   // 行号前缀错误计数（错误诊断用）
    };

    // watcher 父子 this: 窗口关闭时随对象树销毁, 解析任务纯函数无 this
    // 捕获, 完成后结果直接丢弃（回调随 watcher 一并销毁）
    auto *watcher = new QFutureWatcher<ParseResult>(this);
    connect(watcher, &QFutureWatcher<ParseResult>::finished, this,
            [this, watcher, origin]() {
                const ParseResult r = watcher->result();
                watcher->deleteLater();
                startBatchParsed(r.jobs, r.failed, r.lenIssues, r.fmtIssues, origin);
            });
    watcher->setFuture(QtConcurrent::run([templateJob, blocks]() -> ParseResult {
        ParseResult r;
        for (const auto &b : blocks) {
            if (b.line1.length() < 69 || b.line2.length() < 69) {
                ++r.failed;
                ++r.lenIssues;
                continue;
            }
            if (b.line1[0] != '1' || b.line2[0] != '2') {
                ++r.failed;
                ++r.fmtIssues;
                continue;
            }
            TleParser::ParseError parseErr;
            auto tleOpt = TleParser::parse(b.line1.toStdString(), b.line2.toStdString(),
                                           &parseErr);
            if (!tleOpt) { ++r.failed; continue; }
            PassPredictWorker::Job job = templateJob;
            job.tleLine0 = b.name;
            job.tleLine1 = b.line1;
            job.tleLine2 = b.line2;
            r.jobs.append(job);
        }
        return r;
    }));
}

void MainWindow::startBatchParsed(const QVector<PassPredictWorker::Job> &jobs,
                                  int failed, int lenIssues, int fmtIssues,
                                  const QString &origin)
{
    // 解析在途期间可能已发生取消/清除（onCancel/onClear 复位 m_batchMode
    // 并恢复按钮）: 迟到的解析结果作废, 不再启动批次
    if (!m_batchParsePending || !m_batchMode) {
        m_batchParsePending = false;
        return;
    }
    m_batchParsePending = false;

    if (jobs.isEmpty()) {
        QString errorMsg = QString("批量导入失败: 共 %1 颗卫星，全部解析失败\n\n")
            .arg(failed);
        
        // 分析失败原因并提供详细建议（计数由后台解析任务统计）
        errorMsg += "可能的原因:\n";
        
        if (lenIssues > 0) {
            errorMsg += "• TLE行长度不足（每行至少需要69字符）\n";
        }
        
        if (fmtIssues > 0) {
            errorMsg += "• TLE行号错误（行1应以'1'开头，行2应以'2'开头）\n";
        }
        
        errorMsg += "\n建议解决方案:\n";
        errorMsg += "1. 检查TLE数据格式是否符合NORAD标准\n";
        errorMsg += "2. 确保每颗卫星包含完整的两行根数数据\n";
        errorMsg += "3. 尝试从Celestrak或其他可靠来源重新下载TLE\n";
        errorMsg += "4. 使用单颗卫星导入测试具体的错误原因\n";
        
        // 解析期已禁用入口且 m_batchMode 已提前置位（异步化）: 全部失败时复位
        m_batchMode = false;
        m_cancelBtn->setEnabled(false);
        m_predictBtn->setEnabled(true);
        m_batchImportBtn->setEnabled(true);
        m_onlineUpdateBtn->setEnabled(true);
        setExportEnabled(true);
        
        QMessageBox::critical(this, origin, errorMsg);
        return;
    }

    // 批量任务共用 UI 观测站参数, 锁定本次任务坐标（绘图地面标记使用）
    m_taskLatDeg = m_latSpin->value();
    m_taskLonDeg = m_lonSpin->value();

    m_batchMode = true;
    m_batchErrors.clear();
    m_lastErrorStatusText.clear();   // 新批量开始: 错误状态栏去重复位
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    setExportEnabled(false);   // 批量计算期间导出旧数据无意义, 一并禁用
    m_cancelBtn->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, jobs.size());
    m_progressBar->setValue(0);
    // 批量模式不逐星显示历元（onMetaInfo 直接 return）: 须清掉上次单星
    // 任务残留的模型/历元显示（可能带红色"数据过期"样式）, 否则批量期间
    // 状态栏悬挂一颗无关卫星的过期信息, 与"模型: 批量 N 颗"并存产生歧义
    setStatusText(m_statusModel, "模型: —");
    setStatusText(m_statusEpoch, "历元: —");
    m_statusEpoch->setStyleSheet(QString());
    setStatusText(m_statusModel, QString("模型: 批量 %1 颗").arg(jobs.size()));
    if (failed > 0)
        setStatusText(m_statusModel,
            QString("模型: 批量 %1 颗（%2 颗解析失败跳过）")
                .arg(jobs.size()).arg(failed));

    m_service->startBatch(jobs);
}

void MainWindow::onTleError(QString message)
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    setStatusText(m_statusModel, "模型: TLE 下载失败");
    
    // 添加错误恢复建议
    QString detailedMessage = message + "\n\n错误恢复建议:\n";
    detailedMessage += "1. 检查网络连接是否正常\n";
    detailedMessage += "2. 确认Celestrak等数据源服务可用\n";
    detailedMessage += "3. 尝试手动导入TLE文件作为替代方案\n";
    detailedMessage += "4. 稍后重试在线更新功能\n";
    detailedMessage += "5. 联系系统管理员检查网络配置\n";
    
    // 显示详细的错误对话框
    QMessageBox::critical(this, "在线更新失败", detailedMessage);
    
    // 提供恢复选项
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "恢复选项", 
        "是否尝试手动导入TLE文件？",
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    
    if (reply == QMessageBox::Yes) {
        onBatchImport();  // 触发手动导入
    }
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
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    // 清除全部计算线程（内部先协作式取消，毫秒级退出；
    // 2000ms 全局截止防极端情况下无限阻塞 UI）
    if (m_service) {
        m_service->cleanupAll(2000);
    }
    // 中止在途的在线 TLE 下载（无活动请求时静默无效）
    if (m_tleFetcher) {
        m_tleFetcher->cancel();
    }
    m_batchMode = false;
    m_batchParsePending = false;   // 在途批量解析结果作废（startBatchParsed 守卫）
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
    setExportEnabled(true);
    // 下载流程收尾（cancel() 内已 emit finished，此处为空操作保留语义清晰）
}

void MainWindow::onClear()
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    // 增加UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(1);
    
    // 清空视图的同时终止在途计算, 防止迟到的结果覆盖刚清空的界面。
    // 用 cleanupAll(2000) 而非 cancelAll: 在途线程被协作式取消后回收
    // （quit+wait), 反复"开始→清除"不残留空转线程/worker 对象
    if (m_service) {
        m_service->cleanupAll(2000);
    }
    if (m_tleFetcher) {
        m_tleFetcher->cancel();
    }
    
    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    m_batchMode = false;
    m_batchParsePending = false;   // 在途批量解析结果作废
    m_batchErrors.clear();

    m_tleInput1->clear();
    m_tleInput2->clear();
    m_tableModel->setPasses({});   // 清空懒加载 model
    m_currentPasses.clear();
    m_filteredPasses.clear();   // 过滤结果同步清空（M4: 否则内存滞留过境数据）
    
    // 释放数据锁
    dataLocker.unlock();
    
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
    setExportEnabled(true);
    
    // 减少UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(-1);
}

// ============================================================================
// 关闭窗口: 先断开信号槽连接，再协作式取消任务，消除析构链中 cleanupAll 的等待冻结感
// ============================================================================
void MainWindow::closeEvent(QCloseEvent *event)
{
    // 设置关闭标志，通知所有线程正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        m_isClosing = true;
    }
    
    // 唤醒所有等待的线程
    m_dataReadyCondition.wakeAll();
    
    // 首先断开所有信号槽连接，防止在取消过程中有信号触发导致访问已销毁的对象
    disconnectAllConnections();
    
    // 然后取消所有任务
    if (m_service) {
        m_service->cancelAll();      // 立即置协作取消标志, 线程毫秒级退出
    }
    if (m_tleFetcher) {
        m_tleFetcher->cancel();      // 中止在途下载
    }
    
    // 等待导出线程完成（协作式取消已置位, 导出任务毫秒级退出）
    if (m_exportWatcher && m_exportWatcher->isRunning()) {
        m_exportWatcher->waitForFinished();
    }
    
    // 等待所有UI更新操作完成
    while (m_uiUpdateCounter > 0) {
        QThread::msleep(10);
    }
    
    event->accept();
}

// ============================================================================
// 结果处理
// ============================================================================
void MainWindow::onResultsReady(QVector<PassEvent> passes)
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    // 增加UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(1);
    
    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 更新当前过境数据
    m_currentPasses = passes;
    
    // 批量模式收尾状态
    bool wasBatchMode = m_batchMode;
    m_batchMode = false;
    
    // 保存批量错误信息
    QStringList batchErrorsCopy;
    if (wasBatchMode) {
        batchErrorsCopy = m_batchErrors;
        m_batchErrors.clear();
    }
    
    // 释放数据锁
    dataLocker.unlock();
    
    // 应用过滤和刷新
    applyFilterAndRefresh();

    // 批量模式收尾: 汇总失败信息（若有）
    if (wasBatchMode && !batchErrorsCopy.isEmpty()) {
        // 大批量下失败条目可达数千, 弹窗只列前 20 条防止对话框失控
        QStringList shown = batchErrorsCopy.mid(0, 20);
        if (batchErrorsCopy.size() > shown.size())
            shown << QString("…等 %1 项（共 %2 项）")
                       .arg(batchErrorsCopy.size() - shown.size())
                       .arg(batchErrorsCopy.size());
        QMessageBox::warning(this, "批量预报",
            QString("%1 颗卫星预报失败:\n%2")
                .arg(batchErrorsCopy.size())
                .arg(shown.join("\n")));
        // 收尾: 状态栏落最终失败数（节流期间的模数可能未覆盖末条）
        setStatusText(m_statusModel,
                      QString("模型: 批量错误 %1 项").arg(batchErrorsCopy.size()));
    }

    // 恢复控件状态
    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
    setExportEnabled(true);
    
    // 减少UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(-1);
}

void MainWindow::onChecksumWarning(QString satelliteId)
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    // 仅状态栏去重提示: 校验和失败的 TLE 已正常参与计算（非致命）,
    // 不入批量失败汇总、不弹模态框、不复位控件
    showStatusDedup(QString("警告: 卫星 %1 的 TLE 校验和未通过, 计算结果仅供参考")
                        .arg(satelliteId));
}

void MainWindow::onError(QString msg)
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    // 增加UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(1);
    
    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 批量模式处理
    if (m_batchMode) {
        // 批量模式: 不弹阻塞对话框，累计到结果时统一汇总
        m_batchErrors << msg;
        // 状态栏节流: 每 100 项刷一次进度, 防千颗级失败风暴逐条重绘
        // （实时总量由 batchProgress 进度条呈现, 最终失败数由汇总对话框给出）
        const bool throttleTick = (m_batchErrors.size() % 100 == 0);
        
        // 减少UI更新计数器
        m_uiUpdateCounter.fetchAndAddRelaxed(-1);
        // 解锁后再碰 UI（项目约定: 锁内不做 setText 等界面操作）
        dataLocker.unlock();
        if (throttleTick) {
            setStatusText(m_statusModel,
                          QString("模型: 批量错误 %1 项").arg(m_batchErrors.size()));
        }
        return;
    }
    
    // 释放数据锁
    dataLocker.unlock();
    
    // 单星任务错误处理
    QMessageBox::warning(this, "错误", msg);
    // 单星任务失败: 复位上次任务残留在状态栏的模型/历元, 与新错误提示并存
    // 会造成"明明失败了却显示模型/历元正常"的歧义
    setStatusText(m_statusModel, "模型: —");
    setStatusText(m_statusEpoch, "历元: —");
    m_statusEpoch->setStyleSheet(QString());
    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
    setExportEnabled(true);
    
    // 减少UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(-1);
}

void MainWindow::onMetaInfo(QString model, QString epochUtc, double ageDays)
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    // 增加UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(1);
    
    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 批量模式由汇总文本统一展示，跳过逐星元信息
    if (m_batchMode) {
        // 减少UI更新计数器
        m_uiUpdateCounter.fetchAndAddRelaxed(-1);
        return;
    }
    
    // 释放数据锁
    dataLocker.unlock();
    
    setStatusText(m_statusModel, QString("模型: %1").arg(model));
    // 行业惯例: TLE 龄期 > 14 天预报误差可达数 10 秒，需醒目提示
    const bool stale = ageDays > 14.0;
    setStatusText(m_statusEpoch, QString("历元: %1（%2 天）%3")
        .arg(epochUtc).arg(ageDays, 0, 'f', 1)
        .arg(stale ? " [数据过期]" : ""));
    m_statusEpoch->setStyleSheet(stale ? "color:#d04040;" : QString());
    
    // 减少UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(-1);
}

void MainWindow::onTableRowSelected(int row, int col)
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    // 增加UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(1);
    
    Q_UNUSED(col);
    if (row >= 0 && row < m_tableModel->rowCount()) {
        m_polarPlot->setSelectedPass(row);
        m_dopplerPlot->setSelectedPass(row);
        m_groundTrack->setSelectedPass(row);
    }
    
    // 减少UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(-1);
}

// ============================================================================
// 表格填充（懒加载: 数据入 model, 视图按需请求单元格）
// ============================================================================
void MainWindow::populateTable(const QVector<const PassEvent *> &passes)
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    // 增加UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(1);
    
    // 时区表头/时间列由 model 响应 setTimeZoneOffset 自动刷新
    m_tableModel->setTimeZoneOffset(m_timezoneCombo->currentData().toInt());
    m_tableModel->setPasses(passes);
    
    // 减少UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(-1);
}

// ============================================================================
// 导出
// ============================================================================
void MainWindow::setExportEnabled(bool on)
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) return;
    
    m_exportCsvBtn->setEnabled(on);
    m_exportKmlBtn->setEnabled(on);
    m_exportJsonBtn->setEnabled(on);
}

void MainWindow::onExportCSV()
{
    if (m_filteredPasses.isEmpty()) return;
    QString filePath = QFileDialog::getSaveFileName(this, "导出 CSV",
        "passes_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd") + ".csv",
        "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;
    startExport(ExportKind::Csv, filePath);
}

void MainWindow::onExportKML()
{
    if (m_filteredPasses.isEmpty()) return;
    QString filePath = QFileDialog::getSaveFileName(this, "导出 KML",
        "passes_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd") + ".kml",
        "KML Files (*.kml)");
    if (filePath.isEmpty()) return;
    startExport(ExportKind::Kml, filePath);
}

void MainWindow::onExportJSON()
{
    if (m_filteredPasses.isEmpty()) return;
    QString filePath = QFileDialog::getSaveFileName(this, "导出 JSON",
        "passes_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd") + ".json",
        "JSON Files (*.json)");
    if (filePath.isEmpty()) return;
    startExport(ExportKind::Json, filePath);
}

void MainWindow::startExport(ExportKind kind, const QString &filePath)
{
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    // 使用原子操作检查导出锁
    if (m_exportLock.fetchAndAddAcquire(1) != 0) {
        // 如果已经有导出在进行，直接返回
        m_exportLock.fetchAndAddRelease(-1);
        return;
    }
    
    // 计算/下载在途时禁止导出（入口按钮已同步禁用, 此处双保险）:
    // 导出线程直接读共享过境指针, 要求数据在导出期间不可变
    if (m_progressBar->isVisible()) {
        m_exportLock.fetchAndAddRelease(-1);
        return;
    }

    // 锁定一切数据变更入口（清除/计算/下载/取消/导出）:
    // 此后 onResultsReady / onClear 均不可达, m_currentPasses 不可变,
    // 写盘线程解引用共享指针无悬垂风险, 也无需深拷贝快照（零主线程开销）
    setExportEnabled(false);
    m_clearBtn->setEnabled(false);
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);

    if (!m_exportWatcher) {
        m_exportWatcher = std::make_unique<QFutureWatcher<QString>>(this);
        connect(m_exportWatcher.get(), &QFutureWatcher<QString>::finished,
                this, &MainWindow::onExportFinished);
    }
    
    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 指针向量 COW 浅拷贝（8B/项, 微秒级）: 即使主线程重建 m_filteredPasses
    // 也不影响本线程的只读视图; 过境对象本身因入口锁定不可变
    const QVector<const PassEvent *> src = m_filteredPasses;
    const double taskLat = m_taskLatDeg;
    const double taskLon = m_taskLonDeg;
    
    // 释放数据锁
    dataLocker.unlock();
    
    // 使用弱指针模式，确保在导出过程中对象被销毁时能够安全处理
    m_exportWatcher->setFuture(
        QtConcurrent::run([kind, filePath, taskLat, taskLon, src]() -> QString {
            switch (kind) {
            case ExportKind::Csv:  return writePassesCsv(src, filePath);
            case ExportKind::Kml:  return writePassesKml(src, filePath, taskLat, taskLon);
            case ExportKind::Json: return writePassesJson(src, filePath);
            }
            return QString();
        }));
}

void MainWindow::onExportFinished()
{
    // 检查对象是否仍然有效
    if (!this->isVisible()) {
        m_exportLock.fetchAndAddRelease(-1);   // 早退也必须释放（配对缺口会永久禁用导出）
        return;
    }
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) {
            m_exportLock.fetchAndAddRelease(-1);
            return;
        }
    }
    
    // 增加UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(1);
    
    // 释放导出锁
    m_exportLock.fetchAndAddRelease(-1);
    
    // 无论成败都恢复全部入口（仅在 watcher 回调, 无泄漏）;
    // 共享数据指针的生命周期由 startExport 的入口锁定保证
    setExportEnabled(true);
    m_clearBtn->setEnabled(true);
    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    const QString err = m_exportWatcher->result();
    if (!err.isEmpty())
        QMessageBox::warning(this, "导出失败", err);
    
    // 减少UI更新计数器
    m_uiUpdateCounter.fetchAndAddRelaxed(-1);
}

// ============================================================================
// 构造/析构
// ============================================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_uiUpdateCounter(0)
    , m_isClosing(false)
    , m_exportLock(0)
{
    // 初始化错误处理系统
    setupErrorHandling();
    
    setupUI();
    // 确保UI完全设置完成后再建立信号槽连接
    setupConnections();
    
    // 初始化完成，显示欢迎信息
    #ifdef QT_DEBUG
    qDebug() << "MainWindow initialized successfully";
    #endif
}

MainWindow::~MainWindow()
{
    // 设置关闭标志，通知所有线程正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        m_isClosing = true;
    }
    
    // 唤醒所有等待的线程
    m_dataReadyCondition.wakeAll();
    
    // 在析构前断开所有信号槽连接，防止在对象销毁后有信号触发
    disconnectAllConnections();
    
    // 等待所有UI更新操作完成
    while (m_uiUpdateCounter > 0) {
        QThread::msleep(10);
    }
    
    // 清理错误处理相关资源
    ErrorHandler::instance().clearErrorHistory();
    
    // 服务对象由 Qt 父子关系自动管理，无需显式调用
}

void MainWindow::setupErrorHandling()
{
    // 连接错误处理系统的信号
    connect(&ErrorHandler::instance(), &ErrorHandler::errorOccurred,
            this, &MainWindow::onErrorOccurred);
    
    connect(&ErrorHandler::instance(), &ErrorHandler::errorRecoveryAttempt,
            this, &MainWindow::onErrorRecoveryAttempt);
    
    connect(&ErrorHandler::instance(), &ErrorHandler::errorRecoveryResult,
            this, &MainWindow::onErrorRecoveryResult);
    
    // 设置是否启用自动恢复
    ErrorHandler::instance().setAutoRecoveryEnabled(true);
    
    #ifdef QT_DEBUG
    qDebug() << "Error handling system initialized";
    #endif
}

void MainWindow::onErrorOccurred(const ErrorInfo& error)
{
    // 状态栏文本去重（连续相同消息只刷一次）: 批量失败风暴（千颗同因失败）
    // 下错误信号逐条到达, 逐条重绘会占满 UI 线程; 首条已表达全部信息
    // （实现见 showStatusDedup）

    // 根据错误严重程度决定如何处理
    switch (error.severity) {
    case ErrorSeverity::INFO:
        // 信息性错误，只在状态栏显示
        showStatusDedup(QString("信息: %1").arg(error.message));
        break;
        
    case ErrorSeverity::WARNING:
        // 警告错误，显示在状态栏并可能影响UI
        showStatusDedup(QString("警告: %1").arg(error.message));
        
        // 如果是TLE解析警告，可以继续处理
        if (error.type == ErrorType::TLE_PARSE_ERROR) {
            qDebug() << "TLE parse warning (satellite:" << error.satelliteId << "):" << error.message;
        }
        break;
        
    case ErrorSeverity::ERROR:
        // 错误，状态栏显示（阻塞对话框由 worker→service→onError 路径统一处理:
        // 单星弹窗/批量汇总, 避免此处无条件弹窗导致重复弹窗或批量刷屏）
        showStatusDedup(QString("错误: %1").arg(error.message));
        
        // 如果是网络错误，可能需要重试
        if (error.type == ErrorType::NETWORK_ERROR) {
            handleNetworkError(error);
        }
        break;
        
    case ErrorSeverity::CRITICAL:
        // 严重错误，显示严重错误对话框
        showStatusDedup(QString("严重错误: %1").arg(error.message));
        
        // 批量模式下不弹模态（千颗失败风暴 = 千次弹窗刷屏, 状态栏已提示;
        // 当前 CRITICAL 制造路径在生产代码中无调用者, 门控为未来复用防御）
        if (!m_batchMode) {
            QMessageBox::critical(this, "严重错误", 
                                error.message + "\n\n详细信息:\n" + error.detailedContext + 
                                "\n\n建议操作:\n" + error.suggestedAction);
        }
        
        // 对于严重错误，可能需要禁用某些功能
        handleCriticalError(error);
        break;
    }
}

void MainWindow::onErrorRecoveryAttempt(const ErrorInfo& error, RecoveryAction action)
{
    // 显示恢复尝试信息
    QString actionText;
    switch (action) {
    case RecoveryAction::RETRY:
        actionText = "重试";
        break;
    case RecoveryAction::SKIP:
        actionText = "跳过";
        break;
    case RecoveryAction::FALLBACK:
        actionText = "使用备用方案";
        break;
    case RecoveryAction::ABORT:
        actionText = "中止";
        break;
    case RecoveryAction::MANUAL_INTERVENTION:
        actionText = "需要人工干预";
        break;
    }
    
    showStatusDedup(QString("恢复尝试: %1 (%2)").arg(error.message).arg(actionText));
    
    #ifdef QT_DEBUG
    qDebug() << "Recovery attempt for error:" << error.message << "Action:" << actionText;
    #endif
}

void MainWindow::onErrorRecoveryResult(const ErrorInfo& error, bool success)
{
    if (success) {
        showStatusDedup(QString("恢复成功: %1").arg(error.message));
        
        // 如果是TLE解析错误恢复成功，可能需要更新UI
        if (error.type == ErrorType::TLE_PARSE_ERROR) {
            // 可以在这里更新TLE列表或重新计算
        }
    } else {
        // 恢复失败: 仅状态栏提示。错误详情已由 onErrorOccurred 状态栏/
        // 弹窗路径呈现（单星弹窗、批量汇总、CRITICAL 弹窗）——此处若弹
        // 模态对话框, 批量千颗失败会逐条刷屏（用户需点击上千次）
        showStatusDedup(QString("恢复失败: %1").arg(error.message));
    }
}

void MainWindow::handleNetworkError(const ErrorInfo& error)
{
    // 网络错误的特殊处理
    if (error.type == ErrorType::NETWORK_ERROR) {
        // 检查是否应该重试
        bool shouldRetry = ErrorHandler::instance().shouldRetry(error);
        
        if (shouldRetry) {
            // 显示重试对话框
            QMessageBox::StandardButton reply = QMessageBox::question(
                this, "网络错误", 
                error.message + "\n\n是否重试？",
                QMessageBox::Yes | QMessageBox::No);
            
            if (reply == QMessageBox::Yes) {
                // 执行重试逻辑
                retryNetworkOperation(error);
            }
        }
    }
}

void MainWindow::handleCriticalError(const ErrorInfo& error)
{
    // 严重错误的特殊处理
    if (error.type == ErrorType::CONFIG_ERROR) {
        // 配置错误，可能需要重置配置
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "配置错误", 
            error.message + "\n\n是否重置为默认配置？",
            QMessageBox::Yes | QMessageBox::No);
        
        if (reply == QMessageBox::Yes) {
            resetToDefaultConfig();
        }
    }
    
    // 对于其他严重错误，可能需要禁用某些功能
    if (error.type == ErrorType::FILE_IO_ERROR) {
        // 文件IO错误，禁用导出功能
        setExportEnabled(false);
    }
}

void MainWindow::retryNetworkOperation(const ErrorInfo &info)
{
    Q_UNUSED(info);   // Release 下仅 QT_DEBUG 分支使用
    // 重试网络操作: 重新触发当前 TLE 组的在线下载
    //（NETWORK_ERROR 当前生产无触发路径——createNetworkError 无调用者,
    // 本函数为未来网络链路复用预留, 重试语义与 onOnlineUpdate 一致）
    
    #ifdef QT_DEBUG
    qDebug() << "Retrying network operation for error:" << info.message;
    #endif
    
    // 显示重试进度
    setStatusText(m_statusModel, "正在重试网络操作...");
    
    onOnlineUpdate();
}

void MainWindow::resetToDefaultConfig()
{
    // 重置为默认配置的逻辑
    // 这里可以重置所有的UI控件到默认值
    
    #ifdef QT_DEBUG
    qDebug() << "Resetting to default configuration";
    #endif
    
    // 重置观测站位置
    m_latSpin->setValue(39.9042);  // 北京纬度
    m_lonSpin->setValue(116.4074); // 北京经度
    m_altSpin->setValue(0.05);     // 50米海拔
    
    // 重置其他配置
    m_maskSpin->setValue(10.0);    // 10度掩星角
    m_hoursSpin->setValue(24.0);    // 24小时预测
    m_freqSpin->setValue(437.800);  // 437.800 MHz（与 setupUI 默认一致）
    m_stdMagSpin->setValue(4.5);    // 标准星等（与 setupUI 默认一致）
    
    // 重置暗天阈值
    m_darkSkyCombo->setCurrentIndex(0);
    
    setStatusText(m_statusModel, "配置已重置为默认值");
}

#include "MainWindow.moc"
