// =============================================================================
// MainWindow.cpp — 主窗口完整实现
// =============================================================================
#include "MainWindow.h"
#include "PolarPlotWidget.h"
#include "GroundTrackWidget.h"
#include "business/TleBatchParser.h"
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
#include <QFileInfo>
#include <QMessageBox>
#include <QTextStream>
#include <QScrollBar>
#include <QTimeZone>
#include <QCloseEvent>
#include <QtConcurrent>   // 导出文件写入异步化（Qt6 中 QtConcurrent 是独立模块, XGZ.pro 已 QT += concurrent）
#include <QFutureWatcher>
#include <QApplication>     // processEvents
#include <QMenu>            // 过境列表右键菜单（选中某颗卫星的全部过境）
#include <QItemSelectionModel>   // 多选: 导出范围取表格选中行
#include <cmath>          // std::abs（KML 反经线跨越断线）
#include <algorithm>      // std::sort/std::unique（选中行去重排序）
#include <vector>
#include <iterator>       // std::size（列宽数组 static_assert）

// ============================================================================
// TLE 文本入口共用工具（文件局部）
// ============================================================================
namespace {
// TLE 龄期告警阈值（天）: 行业惯例 >14 天预报误差可达数十秒，需醒目提示。
// 单星模式在历元标签标红；批量模式累积超期颗数并汇总（见 onMetaInfo）
constexpr double kStaleAgeDays = 14.0;

// 剥离 UTF-8 BOM（U+FEFF, Windows 记事本"UTF-8 (BOM)"保存常见）:
// U+FEFF 不属于空白字符, trimmed() 不剥; 残留时首行变 "\uFEFF1 ..."
// 无法匹配行号前缀 "1 ", 首颗卫星被误判缺行而丢弃
QString stripUtf8Bom(QString s)
{
    if (s.startsWith(QChar(0xFEFF)))
        s.remove(0, 1);
    return s;
}
} // namespace

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
            m_edit->setPlainText(stripUtf8Bom(QString::fromUtf8(f.readAll())));
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
QString writePassesCsv(const QVector<const PassEvent *> &passes, const QString &path,
                       const QAtomicInt *cancelFlag = nullptr)
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
    out << "#,卫星名,入境(UTC),出境(UTC),持续(s),最大仰角(°),方位角(°),"
           "可见性,太阳高度(°),卫星光照,可见弧段起点(UTC),可见弧段终点(UTC),"
           "入境纬度(°),入境经度(°),入境高度(km),"
           "斜距(km),质量,AOS光照,TCA光照,LOS光照\n";

    // 导出文件里的 UTC 时间串 + RFC4180 引号，格式 "yyyy-MM-dd_HH:mm:ss"。
    // 为什么用下划线而不是空格: Excel/WPS 会把 "2026-09-19 07:24:29" 这种写法解析成
    // **日期值**，并按默认格式 yyyy/m/d h:mm 显示——秒被隐藏，同一分钟的多行看起来时间
    // 完全相同，从 Excel 另存还会真正丢秒（实测 A2.NumberFormat='yyyy/m/d h:mm'、
    // A2.Value2=46284.3…日期序列号；引号挡不住这种类型转换）。
    // 实测在 Excel 中保持纯文本的写法有三种: "2026-09-19_07:24:29"、"… 07:24:29 UTC"、
    // ISO-8601 "2026-09-19T07:24:29Z"；取**不带字母**的下划线写法，既原样显示又好读。
    // pandas / MATLAB: 直接按 "%Y-%m-%d_%H:%M:%S" 解析，或把下划线替换成空格。
    auto jdToStr = [](int64_t jdMs) -> QString {
        return "\"" + QDateTime::fromMSecsSinceEpoch(
                   jdMs - kJdEpochOffsetMs, QTimeZone::UTC)
                   .toString("yyyy-MM-dd_HH:mm:ss") + "\"";
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
        if (cancelFlag && cancelFlag->loadAcquire() != 0)
            return QStringLiteral("导出已取消");
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
                       double taskLatDeg, double taskLonDeg,
                       const QAtomicInt *cancelFlag = nullptr)
{
    // KML 是 XML：卫星名可能含 & < > 等特殊字符，必须转义
    //（名称来自 TLE 名称行或用户粘贴文本，不可信）
    auto xmlEsc = [](const QString &s) { return s.toHtmlEscaped(); };

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("无法写入文件: %1").arg(path);
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    // 坐标数值恒用 '.' 小数点（KML 规范要求）——QTextStream 浮点输出
    // 默认跟随 locale, 逗号小数区域会产出 "116,4074" 非法坐标。
    // 与 CSV 写入器的 setLocale 同源修复
    out.setLocale(QLocale::c());

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
        if (cancelFlag && cancelFlag->loadAcquire() != 0)
            return QStringLiteral("导出已取消");
        const PassEvent *p = passes[i];
        QString desc = QString("Duration: %1 s; Max elev: %2°")
            .arg(p->durationSec).arg(p->maxElevDeg, 0, 'f', 3);
        if (p->hasVisibleArc) {
            desc += QString("; Visible: %1 – %2")
                .arg(QDateTime::fromMSecsSinceEpoch(p->visAosJdMs - kJdEpochOffsetMs,
                                                    QTimeZone::UTC)
                         .toString("HH:mm:ss"))
                .arg(QDateTime::fromMSecsSinceEpoch(p->visLosJdMs - kJdEpochOffsetMs,
                                                    QTimeZone::UTC)
                         .toString("HH:mm:ss"));
        }
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
            // 注意: 绝不能在 segs.append() 之前取 segs.last() 的引用——
            // append 可能触发 QVector 扩容重分配, 旧引用随即悬垂,
            // 之后再 cur.append() 就是往已释放内存里写（堆破坏）。
            // 每一次访问都重新取 last(), 且跨越反经线的点作为**新段首点**
            if (!segs.last().isEmpty()
                && std::abs(lon - segs.last().last().first) > 180.0)
                segs.append(QVector<QPair<double, double>>());
            segs.last().append(qMakePair(lon, lat));
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

// ---------------------------------------------------------------------------
// 逐秒（1 s）详情：详情页签与"含逐秒详情"CSV 导出共用同一套计算，
// 保证页签里看到的数据与导出文件逐点一致
// ---------------------------------------------------------------------------

// 逐秒重传播参数（页面参数快照）: 工作线程只读按值捕获的副本，
// 不跨线程访问 MainWindow 成员
struct DetailPropagationParams {
    std::string tleLine1;
    std::string tleLine2;
    double latDeg   = 0.0;   // 测站纬度/经度/高程
    double lonDeg   = 0.0;
    double altKm    = 0.0;
    double maskDeg  = 0.0;   // 掩星角
    GravityModel gravity = GravityModel::WGS72;   // 与 STK SGP4 默认一致
    int tzOffsetSec = 0;     // 0 = UTC
};

// 逐秒详情行数上限（172800 = 48 h @1 s）: 只对超长弧段生效
// （GEO 单圈可见 24 h ≈ 8.6 万点仍在上限内），防止极端窗口把内存/文件写爆
constexpr int kMaxDetailSteps = 172800;

// 计算某个整秒时刻的 4 列数据（时间/仰角/方位角/距离）
// 返回 false 表示该点无效（传播失败或出现非有限值），由调用方跳过。
// utcTextOut 非空时输出**UTC** 的 "yyyy-MM-dd_HH:mm:ss"（例 2026-09-19_07:24:29），
// 供 CSV 导出使用（Excel 不会把该写法转成日期值，见 jdToStr 处说明）；
// 表内显示仍用带时区的 "yyyy-MM-dd HH:mm:ss"。
inline bool computeDetailRow(Propagator &prop, const Observer &obs, double jd,
                             int tzOffsetSec, QStringList &row,
                             QString *utcTextOut = nullptr)
{
    auto eciOpt = prop.propagate(jd);
    if (!eciOpt) return false;

    const auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
    const auto look = obs.lookAngle(ecef);
    if (!std::isfinite(look.elevDeg) || !std::isfinite(look.azDeg)
        || !std::isfinite(look.rangeKm))
        return false;

    const int64_t jdMs = jdToMs(jd);
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(
        jdMs - kJdEpochOffsetMs, QTimeZone::UTC);
    if (utcTextOut)
        *utcTextOut = dt.toString("yyyy-MM-dd_HH:mm:ss");   // 恒为 UTC，与显示时区无关
    if (tzOffsetSec != 0)
        dt = dt.toTimeZone(QTimeZone(tzOffsetSec));

    row = QStringList{
        dt.toString("yyyy-MM-dd HH:mm:ss"),
        QString::number(look.elevDeg, 'f', 3),
        QString::number(look.azDeg, 'f', 3),
        QString::number(look.rangeKm, 'f', 3)
    };
    return true;
}

// 详情 CSV 导出所需的过境快照（按值捕获到后台线程）。
// 为何需要按值快照: detail 导出在**主 CSV 结束、m_exportLock 释放、入口按钮
// 全部恢复之后**仍会继续跑（每个过境 1 s 逐秒重传播, 千颗级要几十分钟）,
// 此时 m_filteredPasses 的裸指针可能被新一轮预报替换 → 直接持指针即 UAF。
// 但整份 PassEvent 深拷贝会把 samples（15 s 弧段采样, 单过境上百点）一并
// 复制——批量千颗时仅快照就 10 MB 级、主线程卡顿 50~200 ms。
// 逐秒详情是**按 1 s 重新传播**生成的, 从不读 samples,
// 故只快照重传播真正需要的字段（TLE 两行 + 起止时刻）
struct DetailPassRef {
    int         row = -1;       // m_filteredPasses 下标（文件名编号 = row+1）
    int64_t     aosJdMs = 0;
    int64_t     losJdMs = 0;
    std::string tleLine1;
    std::string tleLine2;
};

// 逐秒详情 CSV 导出（单个过境）: 按 1 s 重新传播，口径与"逐秒数据"页签一致。
// 注意: 早前版本直接写 pass.samples（弧段采样点, sampleStepSec = 15 s），
// 文件名却叫 detail/"逐秒详情"——导出文件实际是 15 s 一个点（每分钟 4 行同标签，
// 看起来像"同一时刻有 4 组不同数据"）。现改为与页签同为 1 s。
// 时间列: UTC + "yyyy-MM-dd_HH:mm:ss"（Excel 保持纯文本原样显示，见 jdToStr 处说明）。
QString writePassDetailCsv(const DetailPassRef &pass, const DetailPropagationParams &dp,
                           const QAtomicInt *cancelFlag, const QString &path)
{
    if (dp.tleLine1.empty() || dp.tleLine2.empty())
        return QStringLiteral("TLE 为空，无法重传播: %1").arg(path);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("无法写入文件: %1").arg(path);
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    out.setLocale(QLocale::c());

    out << QString::fromUtf8("时间(UTC),仰角(°),方位角(°),距离(km)\n");

    Propagator prop;
    std::string errMsg;
    if (!prop.initialize(dp.tleLine1, dp.tleLine2, errMsg, dp.gravity))
        return QStringLiteral("TLE 初始化失败: %1")
            .arg(QString::fromStdString(errMsg));
    Observer obs(dp.latDeg, dp.lonDeg, dp.altKm, dp.maskDeg);

    const double jdAOS = msToJd(pass.aosJdMs);
    const double jdLOS = msToJd(pass.losJdMs);
    const double step  = 1.0 / 86400.0;   // 1 s

    int written = 0;
    for (int sec = 0; sec < kMaxDetailSteps; ++sec) {
        const double jd = jdAOS + sec * step;   // 整数索引: 避免 jd += step 的累积漂移
        // 容差 1e-9 天 ≈ 86 µs: 吸收 jdLOS 在 JD≈2.46e6 处的表示舍入, 末行不被漏掉
        if (jd > jdLOS + 1e-9) break;
        if (cancelFlag && cancelFlag->loadAcquire()) break;   // 协作取消: 保留已写内容

        QStringList row;
        QString utcText;
        if (!computeDetailRow(prop, obs, jd, dp.tzOffsetSec, row, &utcText))
            continue;

        // 时间列用 UTC 的 "yyyy-MM-dd_HH:mm:ss": 与表头"时间(UTC)"一致，Excel 不转日期
        out << "\"" << utcText << "\"," << row[1] << ',' << row[2] << ','
            << row[3] << '\n';
        ++written;
    }

    out.flush();
    if (out.status() != QTextStream::Ok || file.error() != QFileDevice::NoError)
        return QStringLiteral("CSV 写入失败: %1").arg(path);
    if (cancelFlag && cancelFlag->loadAcquire())
        return QStringLiteral("导出已取消: %1").arg(path);
    if (written == 0)
        return QStringLiteral("无有效逐秒数据: %1").arg(path);
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
    m_latSpin->setValue(33.0000);
    m_latSpin->setDecimals(4);
    m_latSpin->setSuffix("°");   // 不用 °N: 负值=南纬, 后缀 N 会显示成「-30°N」歧义
    inputLayout->addWidget(m_latSpin, 2, 1);

    inputLayout->addWidget(new QLabel("站经度(°):"), 2, 2);
    m_lonSpin = new QDoubleSpinBox;
    m_lonSpin->setRange(-180.0, 180.0);
    m_lonSpin->setValue(103.0000);
    m_lonSpin->setDecimals(4);
    m_lonSpin->setSuffix("°");   // 同上: 负值=西经
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

    m_visibleOnlyCheck = new QCheckBox("仅显示可见过境");
    inputLayout->addWidget(m_visibleOnlyCheck, 4, 4, 1, 2);

    inputLayout->addWidget(new QLabel("开始时间(UTC):"), 5, 0);
    m_startTimeEdit = new QDateTimeEdit(QDateTime::currentDateTimeUtc());
    m_startTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    m_startTimeEdit->setTimeZone(QTimeZone::UTC);
    inputLayout->addWidget(m_startTimeEdit, 5, 1, 1, 2);

    inputLayout->addWidget(new QLabel("结束时间(UTC):"), 5, 3);
    m_endTimeEdit = new QDateTimeEdit(QDateTime::currentDateTimeUtc().addSecs(24 * 3600));
    m_endTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    m_endTimeEdit->setTimeZone(QTimeZone::UTC);
    inputLayout->addWidget(m_endTimeEdit, 5, 4, 1, 2);

    // 在线更新行
    inputLayout->addWidget(new QLabel("在线更新:"), 6, 0);
    m_tleGroupCombo = new QComboBox;
    for (const auto &g : TleFetcher::groups())
        m_tleGroupCombo->addItem(g.label, g.id);
    m_tleGroupCombo->setToolTip("从 CelesTrak 下载该组卫星的最新 TLE");
    inputLayout->addWidget(m_tleGroupCombo, 6, 1, 1, 3);
    m_onlineUpdateBtn = new QPushButton("下载 TLE");
    m_onlineUpdateBtn->setToolTip(
        "左键: 下载所选组的 TLE（2 小时内的成功下载会直接复用本地缓存——\n"
        "CelesTrak 官方要求同一组数据 2 小时内不要重复拉取）\n"
        "Ctrl+左键 或 右键菜单: 忽略本地缓存强制联网\n"
        "（若 2 小时内已拉取过，可能收到 HTTP 403 限流提示）");
    inputLayout->addWidget(m_onlineUpdateBtn, 6, 4, 1, 2);

    // 重力模型选择（行7）
    inputLayout->addWidget(new QLabel("重力模型:"), 7, 0);
    m_gravModelCombo = new QComboBox;
    // STK 11 的 SGP4 传播器使用 CSSI 例程（WGS-72 常数）: 默认与 STK 一致
    m_gravModelCombo->addItem("WGS-72 (STK 默认)", static_cast<int>(GravityModel::WGS72));
    m_gravModelCombo->addItem("WGS-84", static_cast<int>(GravityModel::WGS84));
    m_gravModelCombo->setToolTip("SGP4/SDP4 重力常数模型\n"
        "WGS-72: STK 11 SGP4 默认（CSSI 例程常数）——要与 STK 逐点对齐请用此项\n"
        "WGS-84: 与 STK 默认不同（24 h 位置差可达数百米）");
    inputLayout->addWidget(m_gravModelCombo, 7, 1);

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
    // 导出范围 = 表格中选中的过境行（未选择任何行时导出全部）
    const QString exportScopeTip =
        "只导出过境列表中选中的行（该行所属卫星及其逐秒详情）；\n"
        "未选择任何行时导出全部过境。\n"
        "提示: 在表格中右键可一键“选中某颗卫星的全部过境”。";
    m_exportCsvBtn->setToolTip(exportScopeTip);
    m_exportKmlBtn->setToolTip(exportScopeTip);
    m_exportDetailCheck = new QCheckBox("含逐秒详情");
    m_exportDetailCheck->setToolTip("CSV 导出时额外为每个过境生成 xxx_detail_N.csv："
                                    "按 1 秒重新传播的逐秒数据（与\"逐秒数据\"页签口径一致）；"
                                    "超长过境按 172800 行（48 h）上限截断");
    m_clearBtn   = new QPushButton("清空");

    btnLayout->addWidget(m_predictBtn);
    btnLayout->addWidget(m_batchImportBtn);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_exportCsvBtn);
    btnLayout->addWidget(m_exportKmlBtn);
    btnLayout->addWidget(m_exportDetailCheck);
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
    const int colWidths[14] = {
        /* # */ 40, /* 卫星名 */ 120, /* 入境 */ 150, /* 出境 */ 150, /* 持续 */ 60,
        /* 最大仰角 */ 75, /* 方位角 */ 60, /* 可见性 */ 50, /* 太阳高度 */ 70, /* 卫星光照 */ 55,
        /* 可见弧段 */ 170, /* 入境位置 */ 165, /* 斜距 */ 85, /* 质量 */ 60
    };
    static_assert(std::size(colWidths) == size_t(PassTableModel::ColCount),
                  "列宽数组必须与 PassTableModel::ColCount 同步增删");
    for (int c = 0; c < PassTableModel::ColCount; ++c)
        m_passTable->setColumnWidth(c, colWidths[c]);
    m_passTable->horizontalHeader()->setMinimumSectionSize(45);
    m_passTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    // 多选: 导出只导出选中行（单颗卫星的多个过境可 Ctrl/Shift 多选，
    // 或用右键菜单一键选中该卫星的全部过境）
    m_passTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_passTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_passTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_passTable->setAlternatingRowColors(false);
    m_passTable->setStyleSheet(
        "QTableView { background-color: white; color: black; }"
        "QTableView::item:selected { background-color: #3399ff; color: white; }"
    );
    m_passTable->verticalHeader()->setVisible(false);
    m_tabWidget->addTab(m_passTable, "过境列表");

    // 页签 2: 极坐标图
    m_polarPlot = new PolarPlotWidget;
    m_tabWidget->addTab(m_polarPlot, "极坐标图");

    // 页签 3: 地面轨迹图
    m_groundTrack = new GroundTrackWidget;
    m_tabWidget->addTab(m_groundTrack, "地面轨迹");

    // 页签 4: 逐秒数据
    m_detailTable = new QTableView;
    m_detailTable->setStyleSheet(
        "QTableView { background-color: white; color: black; }"
        "QTableView::item:selected { background-color: #3399ff; color: white; }");
    m_detailTable->verticalHeader()->setVisible(false);
    m_detailModel = new QStandardItemModel(0, 4, this);
    m_detailModel->setHorizontalHeaderLabels(
        {QString::fromUtf8("时间(UTC)"),
         QString::fromUtf8("仰角(°)"),
         QString::fromUtf8("方位角(°)"),
         QString::fromUtf8("距离(km)")});
    m_detailTable->setModel(m_detailModel);
    m_detailTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tabWidget->addTab(m_detailTable, "逐秒数据");

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
    // "下载 TLE": 点击与右键菜单都接到 lambda（见 setupConnections），
    // 断开时 receiver 传 nullptr 一并拆除
    disconnect(m_onlineUpdateBtn, &QPushButton::clicked, this, nullptr);
    disconnect(m_onlineUpdateBtn, &QWidget::customContextMenuRequested, this, nullptr);
    disconnect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancel);
    disconnect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClear);
    disconnect(m_exportCsvBtn, &QPushButton::clicked, this, &MainWindow::onExportCSV);
    disconnect(m_exportKmlBtn, &QPushButton::clicked, this, &MainWindow::onExportKML);
    
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

    // 取消并断开逐秒详情计算 watcher
    // 判据用 !isFinished(): QtConcurrent 任务"已入队未开跑"时 isRunning() 仍是
    // false, 用 isRunning() 会漏等——worker lambda 捕获了 MainWindow 成员, 不等就 UAF
    if (m_detailWatcher && !m_detailWatcher->isFinished()) {
        m_detailCalcCancel.storeRelease(1);
        m_detailWatcher->waitForFinished();
    }
    if (m_detailWatcher) {
        m_detailWatcher.reset();
    }

    // 断开 ErrorHandler 单例的信号槽连接（单例生命周期长于 MainWindow, 防析构后 UAF）
    disconnect(&ErrorHandler::instance(), &ErrorHandler::errorOccurred, this, nullptr);
    disconnect(&ErrorHandler::instance(), &ErrorHandler::errorRecoveryAttempt, this, nullptr);
    disconnect(&ErrorHandler::instance(), &ErrorHandler::errorRecoveryResult, this, nullptr);
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
    // "下载 TLE": 左键=常规下载（2 小时内复用本地缓存）;
    // Ctrl+左键 = 忽略缓存强制联网; 右键 = 同一条强制刷新的菜单项。
    // 不加常驻控件: CelesTrak 明确要求 2 小时内不要重复拉取同一组数据,
    // 强制刷新属"用户明确知情"的例外通道, 故放在修饰键/右键里
    connect(m_onlineUpdateBtn, &QPushButton::clicked, this, [this]() {
        const bool bypass = QApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
        forceFetchTle(bypass);
    });
    m_onlineUpdateBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_onlineUpdateBtn, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                if (m_isClosing) return;
                QMenu menu(this);
                QAction *act = menu.addAction("忽略本地缓存强制刷新");
                act->setToolTip("CelesTrak 对同一组数据 2 小时内只允许拉取一次，"
                                "强制刷新可能返回 HTTP 403");
                if (menu.exec(m_onlineUpdateBtn->mapToGlobal(pos)) == act)
                    forceFetchTle(true);
            });
    connect(m_tleFetcher.get(), &TleFetcher::downloaded, this, &MainWindow::onTleDownloaded);
    connect(m_tleFetcher.get(), &TleFetcher::fetchError, this, &MainWindow::onTleError);
    // 缓存命中提示: 让用户知道本次未联网（数据新鲜度 + 限流保护已生效），
    // 并告知强制刷新入口（Ctrl+点击 / 右键菜单）
    connect(m_tleFetcher.get(), &TleFetcher::cacheServed, this,
            [this](QString, qint64 ageSec) {
                if (m_isClosing || m_batchMode) return;
                const qint64 mins = ageSec / 60;
                setStatusText(m_statusModel,
                    QString("模型: 使用本地缓存（%1 分钟前下载，CelesTrak 每 ~2 小时更新；"
                            "Ctrl+点击\"下载 TLE\"可强制刷新）")
                        .arg(mins));
            });
    connect(m_tleFetcher.get(), &TleFetcher::finished, this, [this]() {
        // 检查对象是否仍然有效
        if (m_isClosing) return;

        // 批量态（含后台解析窗口期——m_batchMode 已由 startBatchFromBlocks
        // 提前置位）: 入口保持禁用, 收尾由批量完成/失败路径统一负责。
        // 此前无条件恢复会在解析窗口内越权启用预报/导出, 用户点击的
        // 任务随后被 startBatch→cleanupAll 静默替换。
        // 但**按钮文案必须在这里复位**: 仅恢复使能会让按钮永久停在"下载中…"
        if (m_batchMode) {
            m_onlineUpdateBtn->setText("下载 TLE");
            return;
        }

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

    connect(m_service.get(), &PassPredictorService::resultReady,
            this, &MainWindow::onResultsReady);
    connect(m_service.get(), &PassPredictorService::errorOccurred,
            this, &MainWindow::onError);
    connect(m_service.get(), &PassPredictorService::batchProgress,
            this, [this](int completed, int total) {
                // 检查对象是否仍然有效
                if (m_isClosing) return;
                
                m_progressBar->setRange(0, total);
                m_progressBar->setValue(completed);
            });
    connect(m_service.get(), &PassPredictorService::metaInfo,
            this, &MainWindow::onMetaInfo);
    // 点击/键盘方向键/程序化移动统一走 currentRowChanged（model 视图方案）
    connect(m_passTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &) {
                // 检查对象是否仍然有效
                if (m_isClosing) return;
                
                if (cur.isValid()) onTableRowSelected(cur.row(), 0);
            });
    // 过境列表右键菜单: 一键选中该卫星的全部过境（"只导出选中的卫星"用得上）
    connect(m_passTable, &QTableView::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        if (m_isClosing || !m_passTable->selectionModel() || !m_tableModel) return;
        const QModelIndex idx = m_passTable->indexAt(pos);

        QString satName;
        if (idx.isValid()) {
            QMutexLocker locker(&m_dataMutex);
            const int r = idx.row();
            if (r >= 0 && r < m_filteredPasses.size())
                satName = QString::fromStdString(m_filteredPasses[r]->satelliteName);
        }

        QMenu menu(this);
        QAction *actSat = satName.isEmpty() ? nullptr
            : menu.addAction(QString("选中卫星「%1」的全部过境").arg(satName));
        QAction *actOne = idx.isValid() ? menu.addAction("仅选中此过境") : nullptr;
        menu.addSeparator();
        QAction *actClear = menu.addAction("清除选择（导出全部）");
        QAction *chosen = menu.exec(m_passTable->viewport()->mapToGlobal(pos));
        if (!chosen) return;

        QItemSelection sel;
        auto selectRows = [&](const QVector<int> &rows) {
            const int lastCol = m_tableModel->columnCount() - 1;
            for (int r : rows) {
                if (r < 0 || r >= m_tableModel->rowCount()) continue;
                sel.select(m_tableModel->index(r, 0), m_tableModel->index(r, lastCol));
            }
            m_passTable->selectionModel()->select(
                sel, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        };

        if (chosen == actClear) {
            m_passTable->clearSelection();
            showStatusDedup(QStringLiteral("已清除选择：导出将包含全部过境"));
            return;
        }
        if (chosen == actOne && idx.isValid()) {
            selectRows({idx.row()});
            showStatusDedup(QStringLiteral("已选中 1 个过境（导出仅包含该过境）"));
            return;
        }
        if (chosen == actSat && !satName.isEmpty()) {
            QVector<int> rows;
            {
                QMutexLocker locker(&m_dataMutex);
                for (int i = 0; i < m_filteredPasses.size(); ++i)
                    if (QString::fromStdString(m_filteredPasses[i]->satelliteName) == satName)
                        rows.append(i);
            }
            selectRows(rows);
            showStatusDedup(QString("已选中卫星「%1」的 %2 个过境（导出仅包含这些过境）")
                                .arg(satName).arg(rows.size()));
        }
    });
    // 切换"仅显示可见过境"时立即重新过滤并刷新表格/图表
    connect(m_visibleOnlyCheck, &QCheckBox::toggled, this, [this](bool) {
        // 检查对象是否仍然有效
        if (m_isClosing) return;
        
        if (m_currentPasses.isEmpty()) return;
        applyFilterAndRefresh();
    });
    // 时区切换: 无需重新计算, 重建表格时间列与表头即可（H3 修复）
    connect(m_timezoneCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        // 检查对象是否仍然有效
        if (m_isClosing) return;
        
        if (m_currentPasses.isEmpty()) return;
        applyFilterAndRefresh();
    });
}

// ============================================================================
// 按可见性开关过滤并刷新表格/图表/统计
// ============================================================================
void MainWindow::applyFilterAndRefresh(const QString &preserveKey,
                                       int preserveScroll)
{
    if (m_isClosing) return;

    // 过滤条件变更前终止在途逐秒计算: m_filteredPasses 即将重建,
    // 旧计算结果引用的过境指针将失效
    if (m_detailWatcher && !m_detailWatcher->isFinished()) {
        m_detailCalcCancel.storeRelease(1);
        m_detailWatcher->waitForFinished();
    }
    m_detailCalcCancel.storeRelease(0);
    // 世代号递增 + 显式收起进度条: 被取消任务的 finished 回调会在
    // "gen != m_detailGeneration" 处提前 return, 其 setVisible(false) 永不执行;
    // 若此处不收起, 进度条常驻会让 startExport 的闸门永久拦截导出
    ++m_detailGeneration;
    m_progressBar->setVisible(false);

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
    // 必须以「卫星 + 过境时刻」身份键找回原选择。
    // 身份键优先取调用方预快照（onResultsReady 路径: 共享数据已被覆盖,
    // m_filteredPasses 的旧指针已悬垂, 在此解引用即 use-after-free）;
    // 其余调用方共享数据未变, 现场快照安全
    QString prevKey = preserveKey;
    if (prevKey.isEmpty()) {
        const int prevRow = (m_tableModel->rowCount() > 0)
            ? m_passTable->currentIndex().row() : -1;
        if (prevRow >= 0 && prevRow < m_filteredPasses.size()) {
            const auto *prev = m_filteredPasses[prevRow];
            prevKey = QString::number(prev->aosJdMs) + "|" +
                      QString::fromStdString(prev->satelliteName);
        }
    }
    // 滚动位置读取不依赖共享数据; 调用方显式传入时以其为准
    const int prevScroll = (preserveScroll >= 0)
        ? preserveScroll : m_passTable->verticalScrollBar()->value();
    
    // 更新过滤后的数据
    m_filteredPasses = filtered;
    
    // 释放数据锁，允许UI更新
    dataLocker.unlock();
    
    // 刷新UI（在主线程中执行）
    populateTable(m_filteredPasses);
    m_polarPlot->setPasses(m_filteredPasses);
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
        // selectRow 触发 currentRowChanged → onTableRowSelected → syncSelectedPass,
        // 但 setPasses 中的 clamp 可能让绘图控件指向错误行, 且**行号未变时
        // selectRow 不发信号** → 此处必须主动把全部绘图控件同步到当前行
        syncSelectedPass(row);
        // 详情页签: 行号未变、但该行已换成另一颗过境（原选中被"仅显示可见"
        // 过滤掉）时也要重算, 否则逐秒表停留在已被过滤掉的过境数据上
        if (!m_filteredPasses.isEmpty() && row < m_filteredPasses.size()
            && keyOfPass(m_filteredPasses[row]) != prevKey)
            showDetailForPass(row);
    }
    // 恢复滚动位置（模型重置后滚动条归零; 超界值由视图自动钳制）
    m_passTable->verticalScrollBar()->setValue(prevScroll);
}

void MainWindow::setStatusText(QLabel *label, const QString &text)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
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
// 时间窗校验: 起止时间改为 QDateTimeEdit 后, 用户可以设出"结束 ≤ 开始"。
// 此时 hoursAhead 会被 qMax(0.0, …) 钳成 0, 而 OrbitCalculator 对 0 是合法输入,
// 结果是**静默**算完并显示"过境: 0"——用户会误以为该星无过境。
// 单星与批量两条入口都先过这里。
bool MainWindow::ensureValidTimeWindow()
{
    if (m_endTimeEdit->dateTime() > m_startTimeEdit->dateTime()) return true;
    QMessageBox::warning(this, "时间范围错误",
        QString("结束时间必须晚于开始时间。\n\n当前: 开始 %1 / 结束 %2（UTC）")
            .arg(m_startTimeEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss"))
            .arg(m_endTimeEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss")));
    return false;
}

void MainWindow::onPredict()
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 单星粘贴路径同样剥 BOM: 从带 BOM 文件复制首行时 U+FEFF 会随剪贴板带入
    QString tle1 = stripUtf8Bom(m_tleInput1->toPlainText().trimmed());
    QString tle2 = stripUtf8Bom(m_tleInput2->toPlainText().trimmed());

    if (tle1.isEmpty() || tle2.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请输入完整的 TLE 两行根数");
        return;
    }
    if (!ensureValidTimeWindow()) return;

    PassPredictWorker::Job job;
    fillJobFromUi(job);
    job.tleLine1 = tle1;
    job.tleLine2 = tle2;

    // 锁定本次任务的观测站坐标（绘图地面标记使用）
    m_taskLatDeg = m_latSpin->value();
    m_taskLonDeg = m_lonSpin->value();
    m_taskAltKm = m_altSpin->value();
    m_taskMaskDeg = m_maskSpin->value();
    m_taskGravityModel = static_cast<GravityModel>(m_gravModelCombo->currentData().toInt());

    m_batchMode.store(false);
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
    if (m_isClosing) return;
    
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
// 在线更新 TLE（CelesTrak）—— 实现在 forceFetchTle（见下方 forceFetchTle 定义）
// ============================================================================

// 在线更新的唯一实现: bypassCache=true 时忽略本地缓存强制联网
//（入口: Ctrl+点击"下载 TLE" 或 该按钮的右键菜单）
void MainWindow::forceFetchTle(bool bypassCache)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
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
    setStatusText(m_statusModel,
                  bypassCache
                      ? QString("模型: 强制刷新 %1 的 TLE（忽略本地缓存）…")
                            .arg(m_tleGroupCombo->currentText())
                      // 命中 2 小时内的本地缓存时不发网络请求（CelesTrak 限流要求）
                      : QString("模型: 正在获取 %1 的 TLE…")
                            .arg(m_tleGroupCombo->currentText()));
    if (bypassCache)
        m_tleFetcher->refetchGroup(groupId);
    else
        m_tleFetcher->fetchGroup(groupId);
}

// ============================================================================
// TLE 下载完成 → 解析并弹出批量导入对话框（预填文本）
// ============================================================================
void MainWindow::onTleDownloaded(QString rawText, QString groupId)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
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
    if (m_isClosing) return;

    // 时间窗先校验: 批量同样会因 hoursAhead=0 静默算出 0 条过境
    //（在线更新路径会从对话框返回后再进来, 故此处校验对所有入口生效）
    if (!ensureValidTimeWindow()) return;

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
    m_batchMode.store(true);
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

    // watcher 父子 this: 窗口关闭时随对象树销毁;
    // lambda 捕获 this 用于调用 startBatchParsed, 但 closeEvent 已先置 m_isClosing
    // 并断开连接, 析构时 Qt 自动断开剩余信号, 不会触发陈旧回调
    auto *watcher = new QFutureWatcher<ParseResult>(this);
    connect(watcher, &QFutureWatcher<ParseResult>::finished, this,
            [this, watcher, origin]() {
                if (m_isClosing) { watcher->deleteLater(); return; }
                // result() 会重抛工作线程里被 QtConcurrent 捕获的异常
                //（如 bad_alloc）——槽内不捕获会让异常穿出事件循环 → terminate
                ParseResult r;
                try {
                    r = watcher->result();
                } catch (const std::exception &e) {
                    watcher->deleteLater();
                    m_batchParsePending = false;
                    m_batchMode.store(false);
                    m_predictBtn->setEnabled(true);
                    m_batchImportBtn->setEnabled(true);
                    m_onlineUpdateBtn->setEnabled(true);
                    setExportEnabled(true);
                    QMessageBox::critical(this, origin,
                        QString("批量解析异常（已中止本批次）: %1").arg(QString::fromUtf8(e.what())));
                    return;
                } catch (...) {
                    watcher->deleteLater();
                    m_batchParsePending = false;
                    m_batchMode.store(false);
                    m_predictBtn->setEnabled(true);
                    m_batchImportBtn->setEnabled(true);
                    m_onlineUpdateBtn->setEnabled(true);
                    setExportEnabled(true);
                    QMessageBox::critical(this, origin, "批量解析异常（未知类型，已中止本批次）");
                    return;
                }
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
            // 解析口径 = STK 11 定列读取 + 列格式校验（见 core/TleParser.h）
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
        m_batchMode.store(false);
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
    m_taskAltKm = m_altSpin->value();
    m_taskMaskDeg = m_maskSpin->value();
    m_taskGravityModel = static_cast<GravityModel>(m_gravModelCombo->currentData().toInt());

    m_batchMode.store(true);
    m_batchErrors.clear();
    m_lastErrorStatusText.clear();   // 新批量开始: 错误状态栏去重复位
    // 重置批量时效汇总（onMetaInfo 逐星累积）
    m_batchMaxAgeDays = 0.0;
    m_batchStaleCount = 0;
    m_batchMetaCount  = 0;
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    setExportEnabled(false);   // 批量计算期间导出旧数据无意义, 一并禁用
    m_cancelBtn->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, jobs.size());
    m_progressBar->setValue(0);
    // 批量模式不逐星显示历元，改为在 onMetaInfo 里累积批量级汇总
    //（最老龄期 + 超期颗数, 见该函数）: 此处须清掉上次单星任务残留的
    // 模型/历元显示（可能带红色"数据过期"样式）, 否则批量期间状态栏悬挂
    // 一颗无关卫星的过期信息, 与"模型: 批量 N 颗"并存产生歧义
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
    if (m_isClosing) return;
    
    setStatusText(m_statusModel, "模型: TLE 下载失败");
    
    // 添加错误恢复建议
    QString detailedMessage = message + "\n\n错误恢复建议:\n";
    detailedMessage += "1. 检查网络连接是否正常\n";
    detailedMessage += "2. 确认Celestrak等数据源服务可用\n";
    detailedMessage += "3. 尝试手动导入TLE文件作为替代方案\n";
    detailedMessage += "4. 稍后重试在线更新功能\n";
    detailedMessage += "5. 联系系统管理员检查网络配置\n";
    
    // 显示错误对话框，同时提供恢复选项（合并为单个模态框，避免双弹窗）
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "在线更新失败", detailedMessage + "\n\n是否尝试手动导入TLE文件作为替代？",
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
    job.startUtc       = m_startTimeEdit->dateTime();
    job.hoursAhead     = qMax(0.0, job.startUtc.secsTo(m_endTimeEdit->dateTime()) / 3600.0);

    // 配置 OrbitCalculator 参数
    // 粗搜步长 15s: 高掩蔽角 + 低轨的短过境（窗口 1~2 分钟）也能可靠检出
    //（60s 步长会概率性漏检；每颗卫星全窗口约 0.2s，批量并行可接受）
    job.calcCfg.coarseStepSec      = 15.0;
    job.calcCfg.bisectionIters     = 18;
    job.calcCfg.enableSunAnalysis  = true;
    job.calcCfg.enableShadowCheck  = true;
    job.calcCfg.darkSkyThresholdDeg = m_darkSkyCombo->currentData().toDouble();
    job.calcCfg.gravityModel = static_cast<GravityModel>(m_gravModelCombo->currentData().toInt());
}

void MainWindow::onCancel()
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 清除全部计算线程（内部先协作式取消，毫秒级退出；
    // 2000ms 全局截止防极端情况下无限阻塞 UI）
    if (m_service) {
        m_service->cleanupAll(2000);
    }
    // 中止在途的在线 TLE 下载（无活动请求时静默无效）
    if (m_tleFetcher) {
        m_tleFetcher->cancel();
    }
    m_batchMode.store(false);
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
    // 导出进行中不恢复导出按钮: 防止 UI 状态瞬间不一致,
    // onExportFinished 会在导出结束后统一恢复。
    // 判据用 !isFinished(): QtConcurrent 任务"已入队未开跑"时 isRunning() 仍为
    // false, 用 isRunning() 会误判"无导出任务"而提前放开导出按钮（同一 future
    // 句柄被下一次导出覆盖 → 旧任务成为无人等待的孤儿）
    if (!m_exportWatcher || m_exportWatcher->isFinished())
        setExportEnabled(true);
    // 下载流程收尾（cancel() 内已 emit finished，此处为空操作保留语义清晰）
}

void MainWindow::onClear()
{
    if (m_isClosing) return;
    // 共享数据"换血中"窗口（onResultsReady 的 waitForFinished → 数据交换）:
    // 此刻新结果正在路上、旧数据尚未替换, 清空会与随后写入的新数据打架
    //（旧行为: 清除后新一轮结果又冒出来, 界面处于"清一半"的错乱状态）。
    // 该窗口至多持续一次逐秒计算的时长, 期间丢弃清除请求是更安全的选择
    if (m_sharedDataWaitDepth.load() > 0) return;

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
    m_batchMode.store(false);
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
    m_groundTrack->clear();
    m_detailModel->removeRows(0, m_detailModel->rowCount());
    // 清空后不再有"当前过境": 逐秒页签标题复位（去掉历元标注）
    if (const int tabIdx = m_tabWidget->indexOf(m_detailTable); tabIdx >= 0) {
        m_tabWidget->setTabText(tabIdx, "逐秒数据");
        m_tabWidget->setTabToolTip(tabIdx, QString());
    }
    // 取消逐秒详情后台计算。
    // 判据 !isFinished(): "已入队未开跑"时 isRunning() 为 false, 漏等会让该任务
    // 在下面 storeRelease(0) 之后带着"未取消"状态起跑——它捕获了本对象成员,
    // 并可能把陈旧行回填进刚清空的逐秒表（世代号只能拦住交付, 拦不住执行）
    if (m_detailWatcher && !m_detailWatcher->isFinished()) {
        m_detailCalcCancel.storeRelease(1);
        m_detailWatcher->waitForFinished();
    }
    m_detailCalcCancel.storeRelease(0);
    // 关键: waitForFinished() 只阻塞、不处理事件——被取消任务的 finished 回调
    // 必然在 onClear 返回后才投递。此处必须递增世代号, 否则回调里
    // "gen != m_detailGeneration" 守卫失效（取消标志也已被上面清 0）,
    // 陈旧的部分结果会被回填进刚清空的逐秒表, 并误隐藏进度条
    ++m_detailGeneration;
    // 重置任务快照（防御性: 防止旧参数残留影响后续逻辑）
    m_taskLatDeg = m_latSpin->value();
    m_taskLonDeg = m_lonSpin->value();
    m_taskAltKm = m_altSpin->value();
    m_taskMaskDeg = m_maskSpin->value();
    m_taskGravityModel = static_cast<GravityModel>(m_gravModelCombo->currentData().toInt());
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
}

// ============================================================================
// 关闭窗口: 先断开信号槽连接，再协作式取消任务，消除析构链中 cleanupAll 的等待冻结感
// ============================================================================
void MainWindow::closeEvent(QCloseEvent *event)
{
    // 设置关闭标志，通知所有线程正在关闭
    m_isClosing.store(true);

    // 首先断开所有信号槽连接，防止在取消过程中有信号触发导致访问已销毁的对象
    disconnectAllConnections();
    
    // 然后取消所有任务
    if (m_service) {
        m_service->cancelAll();      // 立即置协作取消标志, 线程毫秒级退出
    }
    // 线程退出等待由 ~PassPredictorService 的 cleanupAll(2000) 兜底;
    // 此处不阻塞主线程，避免关闭时界面冻结（disconnectAllConnections 已阻断旧信号）
    if (m_tleFetcher) {
        m_tleFetcher->cancel();      // 中止在途下载
    }
    
    // 等待导出线程完成（协作式取消已置位, 导出任务毫秒级退出）
    // 注: 导出函数为同步写入，取消标志仅用于快速退出判断
    // 判据 !isFinished(): 见 disconnectAllConnections 处说明（入队未开跑时 isRunning() 为 false）
    if (m_exportWatcher && !m_exportWatcher->isFinished()) {
        m_exportCancel.storeRelease(1);
        m_exportWatcher->waitForFinished();
    }
    // 注: 此处**故意不**把 m_exportLock 减回 0。锁的契约是"每次成功启动的
    // 导出由 onExportFinished 配对释放", 而本路径已 disconnectAllConnections,
    // 延迟投递的 onExportFinished 不会再执行——若在此手工清零, 一旦某条路径
    // 仍送达 onExportFinished 就会把计数减成负数。锁随本对象析构一并消失,
    // 不存在跨对象泄漏
    // detail CSV 后台导出: lambda 捕获 QPointer<MainWindow>, 析构后自动跳过,
    // 但仍须等待 future 完成, 防止工作线程在 ~MainWindow 成员销毁后仍访问
    if (!m_detailCsvFuture.isFinished()) {
        m_detailCsvCancel.storeRelease(1);  // 协作取消: 置位后 lambda 在下一过境边界退出
        m_detailCsvFuture.waitForFinished();
    }
    // 批量后台解析任务（startBatchFromBlocks 的 QtConcurrent）无需等待:
    // watcher 为 this 子对象, closeEvent 已先断开连接并置 m_isClosing,
    // 回调入口有 m_isClosing 守卫; 进程退出时 QThreadPool 全局池析构会
    // 兜底等待（千颗级解析延迟数百 ms, 属可接受的设计取舍）

    event->accept();
}

// ============================================================================
// 结果处理
// ============================================================================
void MainWindow::onResultsReady(QVector<PassEvent> passes)
{
    if (m_isClosing) return;

    // 导出守卫: startExport 已快照共享数据并锁定 UI, 此后到达的 resultReady
    // 若覆盖 m_currentPasses 会导致导出线程裸指针悬垂（UAF）。丢弃即可——
    // 导出守卫: startExport 已快照共享数据并锁定 UI, 此后到达的 resultReady
    // 若覆盖 m_currentPasses 会导致导出线程裸指针悬垂（UAF）。丢弃即可——
    // 进度条/按钮已锁定, 用户无法发起新操作, 数据快照已在 startExport 中完成
    if (m_exportLock.loadAcquire() != 0) return;

    // 注: 此处**不**检查 m_sharedDataWaitDepth——两轮结果相遇时由世代号
    // 决定"后到者接管"（见下）, 若在此按 busy 短路会把新结果整份丢掉。

    // 世代守卫: waitForFinished() 期间若真的处理了事件, 可能触发新的
    // onResultsReady, 通过世代号检测丢弃过期调用，避免重入导致 stale preserveKey
    const int expected = m_resultsGeneration.fetchAndAddRelease(1) + 1;

    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);

    // 预快照选中身份键与滚动位置: 必须在覆盖 m_currentPasses【之前】——
    // 赋值后 QVector COW 缓冲更换, 旧 PassEvent 全部析构, m_filteredPasses
    // 内的裸指针立即悬垂; 若让 applyFilterAndRefresh 自行快照即触发
    // use-after-free（第二次预报起每次必现）
    QString preserveKey;
    {
        const int prevRow = (m_tableModel->rowCount() > 0)
            ? m_passTable->currentIndex().row() : -1;
        if (prevRow >= 0 && prevRow < m_filteredPasses.size()) {
            const auto *prev = m_filteredPasses[prevRow];
            preserveKey = QString::number(prev->aosJdMs) + "|" +
                          QString::fromStdString(prev->satelliteName);
        }
    }
    const int preserveScroll = m_passTable->verticalScrollBar()->value();

    // 批量模式收尾状态
    bool wasBatchMode = m_batchMode.load();
    m_batchMode.store(false);
    
    // 保存批量错误信息
    QStringList batchErrorsCopy;
    if (wasBatchMode) {
        batchErrorsCopy = m_batchErrors;
        m_batchErrors.clear();
    }

    // 先取消正在运行的逐秒 watcher, 再更新共享数据——
    // 此前的顺序（先更新 m_filteredPasses 再发取消）在 waitForFinished()
    // 期间派发事件循环时可能被新的 onResultsReady 重入，第二轮更新
    // m_filteredPasses 后第一批 worker 仍读取已替换的 COW 缓冲（UAF）。
    // 正确顺序: 取消 → waitForFinished → 更新（锁内），确保 worker 停止后
    // 数据才被替换
    bool inDataSwapWindow = false;
    if (m_detailWatcher && !m_detailWatcher->isFinished()) {
        m_detailCalcCancel.storeRelease(1);
        // 释放锁后再 waitForFinished: 持锁等待可能死锁（worker 完成后
        // 需要主线程事件循环处理 finished 信号，但锁阻止了新事件处理）
        dataLocker.unlock();
        // 窗口计数 +1: 此刻起 m_dataMutex **未被持有**且共享数据仍是旧值。
        // onClear / showDetailForPass 等改写槽据此短路, 避免"清空"被随后的
        // 数据交换静默撤销。计数在**数据交换完成并解锁之后**才归零
        m_sharedDataWaitDepth.fetch_add(1);
        inDataSwapWindow = true;
        m_detailWatcher->waitForFinished();
        // 重入检测: 等待期间若新 resultReady 已到达, 本轮作废（新的一轮接管）
        if (m_resultsGeneration.loadAcquire() != expected) {
            m_sharedDataWaitDepth.fetch_sub(1);
            m_detailCalcCancel.storeRelease(0);
            ++m_detailGeneration;
            return;
        }
        // 重新获取锁: 更新共享数据
        dataLocker.relock();
    }
    m_detailCalcCancel.storeRelease(0);
    ++m_detailGeneration;

    // 在同一临界区内完成: 更新 m_currentPasses → 重建 m_filteredPasses
    // 确保 m_filteredPasses 的裸指针永远指向 m_currentPasses 中存活的对象
    m_currentPasses = passes;
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
    m_filteredPasses = filtered;
    dataLocker.unlock();
    if (inDataSwapWindow)
        m_sharedDataWaitDepth.fetch_sub(1);   // 数据已换完, 恢复正常状态

    // 清空详情页签旧数据（新结果到达时，旧选中的过境已失效）
    m_detailModel->removeRows(0, m_detailModel->rowCount());
    // 页签标题同步复位（去掉上一轮过境的"历元 …"标注，与 onClear 行为一致）
    if (const int tabIdx = m_tabWidget->indexOf(m_detailTable); tabIdx >= 0) {
        m_tabWidget->setTabText(tabIdx, "逐秒数据");
        m_tabWidget->setTabToolTip(tabIdx, QString());
    }

    // 应用过滤和刷新（传入预快照, 避免内部读取已悬垂的旧指针）
    applyFilterAndRefresh(preserveKey, preserveScroll);

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
}

void MainWindow::onError(QString msg)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }

    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 批量模式处理
    if (m_batchMode) {
        // 批量模式: 不弹阻塞对话框，累计到结果时统一汇总
        m_batchErrors << msg;
        // 状态栏节流: 每 100 项刷一次进度, 防千颗级失败风暴逐条重绘
        // （实时总量由 batchProgress 进度条呈现, 最终失败数由汇总对话框给出）
        const bool throttleTick = (m_batchErrors.size() % 100 == 0);

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
}

void MainWindow::onMetaInfo(QString model, QString epochUtc, double ageDays)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }

    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 批量模式: 不逐星展示（否则状态栏被上千颗卫星的历元刷屏），
    // 但**必须**保留时效风险提示——逐星累积"最老龄期 + 超期颗数"，
    // 在状态栏给出批量级汇总（此前直接 return, 批量用户完全看不到
    // "数据过期"警告, 是最严重的告警通道缺口）
    if (m_batchMode) {
        if (ageDays > m_batchMaxAgeDays) m_batchMaxAgeDays = ageDays;
        if (ageDays > kStaleAgeDays)     ++m_batchStaleCount;
        ++m_batchMetaCount;
        const double maxAge = m_batchMaxAgeDays;
        const int staleCount = m_batchStaleCount;
        const int metaCount  = m_batchMetaCount;
        dataLocker.unlock();

        QString text = QString("历元: 批量 %1 颗（最旧 %2 天）")
                           .arg(metaCount).arg(maxAge, 0, 'f', 1);
        if (staleCount > 0)
            text += QString(" [数据过期 %1 颗]").arg(staleCount);
        setStatusText(m_statusEpoch, text);
        m_statusEpoch->setStyleSheet(staleCount > 0 ? "color:#d04040;" : QString());
        return;
    }
    
    // 释放数据锁
    dataLocker.unlock();
    
    setStatusText(m_statusModel, QString("模型: %1").arg(model));
    // 行业惯例: TLE 龄期 > 14 天预报误差可达数 10 秒，需醒目提示
    const bool stale = ageDays > kStaleAgeDays;
    setStatusText(m_statusEpoch, QString("历元: %1（%2 天）%3")
        .arg(epochUtc).arg(ageDays, 0, 'f', 1)
        .arg(stale ? " [数据过期]" : ""));
    m_statusEpoch->setStyleSheet(stale ? "color:#d04040;" : QString());
}

void MainWindow::onTableRowSelected(int row, int col)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    Q_UNUSED(col);
    if (row >= 0 && row < m_tableModel->rowCount()) {
        syncSelectedPass(row);
        showDetailForPass(row);
    }
}

// 同步"当前选中过境"到全部绘图控件。
// 三处入口（表格选中 / 过滤刷新 / 结果更新）必须一起调用——逐个手写
// setSelectedPass 极易漏掉一个（历史上 applyFilterAndRefresh 就漏了地面轨迹），
// 漏掉会让该控件的选中索引停留在上一次的行号、高亮到另一颗过境。
void MainWindow::syncSelectedPass(int row)
{
    m_polarPlot->setSelectedPass(row);
    m_groundTrack->setSelectedPass(row);
}

// 过境身份键（卫星名 + 入境时刻）: 过滤/时区切换后行序会变, 只能按身份找回
QString MainWindow::keyOfPass(const PassEvent *p)
{
    if (!p) return QString();
    return QString::number(p->aosJdMs) + "|" + QString::fromStdString(p->satelliteName);
}

// ============================================================================
// 逐秒详情视图：点击过境行后按需重新传播，显示每秒的观测数据
// ============================================================================
void MainWindow::showDetailForPass(int row)
{
    // 共享数据"换血中"窗口: 该窗口内 m_filteredPasses 仍是旧集合、即将被
    // 新结果整体替换, 此刻按行号启动逐秒计算既会读到即将失效的行, 也会
    // 抢改 onResultsReady 正在等待的 m_detailWatcher（世代号会作废它,
    // 但白跑一趟重传播） → 直接丢弃
    if (m_sharedDataWaitDepth.load() > 0) return;

    // 取消上一次未完成的逐秒计算。
    // 判据 !isFinished(): 入队未开跑的旧任务若不等待, 会在下面清掉取消标志后
    // 与新任务并发执行同一个 future 句柄（旧任务成为孤儿, 且两路同时写进度/表）
    if (m_detailWatcher && !m_detailWatcher->isFinished()) {
        m_detailCalcCancel.storeRelease(1);
        m_detailWatcher->waitForFinished();
    }
    m_detailCalcCancel.storeRelease(0);
    ++m_detailGeneration;   // 作废旧回调

    // 清空旧数据
    m_detailModel->removeRows(0, m_detailModel->rowCount());

    // 在锁内拷贝所需字段，防止 m_filteredPasses 被并发覆盖后 UAF
    std::string tleLine1, tleLine2;
    int64_t aosJdMs = 0, losJdMs = 0;
    double taskLat = 0, taskLon = 0, taskAlt = 0, taskMask = 0;
    GravityModel taskGrav = GravityModel::WGS72;   // 与主结果所用模型一致（默认 = STK 的 WGS-72）
    int tzOffsetSec = 0;
    int tleEpochYear = 0;      // TLE 历元随过境一同携带（无需再解析一次 TLE）
    double tleEpochDay = 0.0;
    {
        QMutexLocker locker(&m_dataMutex);
        if (row < 0 || row >= m_filteredPasses.size()) return;
        const PassEvent *ev = m_filteredPasses[row];
        tleLine1 = ev->tleLine1;
        tleLine2 = ev->tleLine2;
        tleEpochYear = ev->tleEpochYear;
        tleEpochDay  = ev->tleEpochDay;
        aosJdMs  = ev->aosJdMs;
        losJdMs  = ev->losJdMs;
        taskLat  = m_taskLatDeg;
        taskLon  = m_taskLonDeg;
        taskAlt  = m_taskAltKm;
        taskMask = m_taskMaskDeg;
        taskGrav = m_taskGravityModel;   // 详情重传播必须复用主结果所用模型
        if (m_timezoneCombo->currentIndex() > 0)
            tzOffsetSec = m_timezoneCombo->currentData().toInt();
    }

    // TLE 为空则跳过（防御性检查）
    if (tleLine1.empty() || tleLine2.empty()) return;

    // 逐秒数据页签标注**本次实际使用的 TLE 历元**:
    // 跨工具（STK/Gpredict…）逐秒比对前必须先确认两边用的是同一份 TLE——
    // 历元差几小时/几天即可造成 km 级沿迹位置差（远超实现差异）。
    // 显示为 "逐秒数据 [历元 yyyy-MM-dd HH:mm, N.N 天]"，便于直接对照
    {
        // 历元由 worker 解析 TLE 时随过境带出（PassEvent::tleEpochYear/Day）,
        // 此处不再为了一行标题重复走一遍完整解析（长度/范围/校验和/B*）
        int year = tleEpochYear;
        double day = tleEpochDay;
        if (year <= 0 || !(day > 0.0)) {
            // 兜底: 未携带历元的过境（外部构造/旧数据）才现解析一次
            TleParser::ParseError pe;
            const auto tleMeta = TleParser::parse(tleLine1, tleLine2, &pe);
            if (tleMeta) {
                year = tleMeta->epochYear;
                day  = tleMeta->epochDay;
            }
        }
        if (year > 0 && day > 0.0) {
            const QDateTime yearStart(QDate(year, 1, 1), QTime(0, 0), QTimeZone::UTC);
            const QDateTime epochUtc = yearStart.addMSecs(
                static_cast<qint64>((day - 1.0) * 86400000.0 + 0.5));
            const double ageDays = epochUtc.secsTo(QDateTime::currentDateTimeUtc()) / 86400.0;
            const int tabIdx = m_tabWidget->indexOf(m_detailTable);
            if (tabIdx >= 0) {
                m_tabWidget->setTabText(tabIdx,
                    QString("逐秒数据 [历元 %1, %2 天]")
                        .arg(epochUtc.toString("yyyy-MM-dd HH:mm"))
                        .arg(ageDays, 0, 'f', 1));
                m_tabWidget->setTabToolTip(tabIdx,
                    QString("TLE 历元(UTC): %1\n数据龄期: %2 天\n"
                            "跨工具比对请确认两侧 TLE 历元/元素集完全一致")
                        .arg(epochUtc.toString("yyyy-MM-dd HH:mm:ss 'UTC'"))
                        .arg(ageDays, 0, 'f', 2));
            }
        }
    }

    // 计算总步数用于进度条
    const double jdAOS = msToJd(aosJdMs);
    const double jdLOS = msToJd(losJdMs);
    const int totalSteps = std::min(static_cast<int>((jdLOS - jdAOS) * 86400.0) + 1,
                                    kMaxDetailSteps);

    // 显示进度条（确定进度模式）
    m_progressBar->setRange(0, totalSteps);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);

    // 创建/复用 watcher
    if (!m_detailWatcher)
        m_detailWatcher = std::make_unique<QFutureWatcher<void>>(this);
    else
        m_detailWatcher->disconnect();

    auto result = std::make_shared<QVector<QStringList>>();
    const int gen = m_detailGeneration.loadAcquire();

    connect(m_detailWatcher.get(), &QFutureWatcher<void>::finished, this,
        [this, result, gen]() {
            if (m_isClosing) return;
            if (gen != m_detailGeneration.loadAcquire()) return;   // 已被新请求作废
            m_progressBar->setVisible(false);
            if (m_detailCalcCancel.loadAcquire()) return;
            if (result->isEmpty()) {
                m_detailModel->setRowCount(1);
                m_detailModel->setItem(0, 0, new QStandardItem(
                    QStringLiteral("计算完成，无有效数据")));
                return;
            }
            // 填充模型（固定 4 列: 时间/仰角/方位角/距离）
            constexpr int cols = 4;
            m_detailModel->setRowCount(result->size());
            for (int i = 0; i < result->size(); ++i) {
                for (int c = 0; c < cols; ++c)
                    m_detailModel->setItem(i, c, new QStandardItem((*result)[i][c]));
            }
        });

    // 后台线程: 逐秒传播 + 坐标转换 + 格式化
    // （行上限 kMaxDetailSteps 与详情 CSV 导出共用, 定义在文件局部）
    m_detailWatcher->setFuture(QtConcurrent::run(
        [this, result, tleLine1, tleLine2, jdAOS, jdLOS,
         taskLat, taskLon, taskAlt, taskMask, taskGrav,
         tzOffsetSec]() {
        QPointer<MainWindow> safeThis(this);

        try {
            Propagator prop;
            std::string errMsg;
            // 复用主结果所用重力模型: 否则详情表与表格/极坐标口径不一致
            //（WGS72 μ=398600.8/Re=6378.135 vs WGS84 μ=398600.5/Re=6378.137）
            if (!prop.initialize(tleLine1, tleLine2, errMsg, taskGrav)) return;

            Observer obs(taskLat, taskLon, taskAlt, taskMask);
            const double step = 1.0 / 86400.0;

            int iterCount = 0;
            // 逐秒时刻用"起点 + 整数秒 × 步长": jd += step 会累积浮点漂移
            // （数百步即达亚毫秒级, 与 STK 逐秒数据比对时可见），此处必须精确
            for (int sec = 0; sec < kMaxDetailSteps; ++sec) {
                const double jd = jdAOS + sec * step;
                // 容差 1e-9 天 ≈ 86 µs: 仅用于吸收 jdLOS 在 JD≈2.46e6 处的
                // 表示舍入(ulp≈40 µs), 保证整秒窗口的末行不被漏掉
                if (jd > jdLOS + 1e-9) break;
                if (m_detailCalcCancel.loadAcquire()) return;

                // 与"含逐秒详情"CSV 导出共用同一函数: 页签与导出文件逐点一致
                QStringList row;
                if (computeDetailRow(prop, obs, jd, tzOffsetSec, row))
                    result->append(row);

                ++iterCount;
                if (iterCount % 500 == 0) {
                    QMetaObject::invokeMethod(m_progressBar,
                        [safeThis, iterCount]() {
                            if (safeThis) safeThis->m_progressBar->setValue(iterCount);
                        },
                        Qt::QueuedConnection);
                }
            }
        } catch (...) {
        }
    }));
}

// ============================================================================
// 表格填充（懒加载: 数据入 model, 视图按需请求单元格）
// ============================================================================
void MainWindow::populateTable(const QVector<const PassEvent *> &passes)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    // 时区表头/时间列由 model 响应 setTimeZoneOffset 自动刷新
    m_tableModel->setTimeZoneOffset(m_timezoneCombo->currentData().toInt());
    m_tableModel->setPasses(passes);
}

// ============================================================================
// 导出
// ============================================================================
void MainWindow::setExportEnabled(bool on)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    m_exportCsvBtn->setEnabled(on);
    m_exportKmlBtn->setEnabled(on);
}

void MainWindow::onExportCSV()
{
    if (m_filteredPasses.isEmpty()) return;
    QString filePath = QFileDialog::getSaveFileName(this, "导出 CSV",
        "passes_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd") + ".csv",
        "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    // 导出范围与状态栏提示由 startExport 决定（有选中行 → 仅选中过境）;
    // 逐秒详情用同一份范围，保证"主表里有哪些过境，就有哪些 detail 文件"
    // 勾选状态在 startExport **之前**读取: startExport 内部可能弹状态栏提示
    // 等（未来若引入嵌套事件循环，用户可能在对话框未关时改勾选），
    // 本函数后续动作必须与"发起导出时的勾选状态"一致
    const bool wantDetail = m_exportDetailCheck->isChecked();

    QVector<int> detailRows;
    // 用**返回值**判断主导出是否真的启动（旧实现读 m_exportLock 间接判断:
    // 那是"锁恰好被本函数持有"的隐式契约, 一旦 startExport 新增早退路径
    // 或改成延迟持锁就会失效）；范围说明文本由 startExport 直接进状态栏
    const bool exportStarted = startExport(ExportKind::Csv, filePath, &detailRows);

    // 勾选"含逐秒详情"时，为**本次导出的每个过境**生成独立 detail CSV
    //（后台线程按 1 s 重传播写盘）
    if (wantDetail && exportStarted) {
        // 轻量按值快照（TLE 两行 + 起止时刻, 见 DetailPassRef 说明）:
        // 详情导出会远晚于主 CSV 结束、在导出锁释放之后继续跑, 因此必须按值
        // 快照而非持 m_filteredPasses 指针; 但逐秒详情是按 1 s 重新传播生成的,
        // 不读 samples —— 此前整份深拷贝 PassEvent 会把上百个弧段采样点一并
        // 复制, 批量千颗时快照本身就要 10 MB 级、主线程卡顿 50~200 ms
        QVector<DetailPassRef> snapshot;
        DetailPropagationParams dp;
        QFileInfo fi(filePath);
        QString baseName = fi.completeBaseName();  // 不含扩展名
        QString dir = fi.absolutePath();
        {
            QMutexLocker locker(&m_dataMutex);
            snapshot.reserve(detailRows.size());
            for (int r : detailRows) {
                if (r < 0 || r >= m_filteredPasses.size()) continue;
                const PassEvent *src = m_filteredPasses[r];
                DetailPassRef ref;
                ref.row = r;
                ref.aosJdMs  = src->aosJdMs;
                ref.losJdMs  = src->losJdMs;
                ref.tleLine1 = src->tleLine1;
                ref.tleLine2 = src->tleLine2;
                snapshot.append(std::move(ref));
            }
            dp.latDeg  = m_taskLatDeg;
            dp.lonDeg  = m_taskLonDeg;
            dp.altKm   = m_taskAltKm;
            dp.maskDeg = m_taskMaskDeg;
            dp.gravity = m_taskGravityModel;   // 详情重传播必须复用主结果所用模型
            if (m_timezoneCombo->currentIndex() > 0)
                dp.tzOffsetSec = m_timezoneCombo->currentData().toInt();
        }
        QPointer<MainWindow> safeThis(this);
        // 上一轮详单导出仍在进行时, 必须先取消并等待完成:
        // 否则"清取消标志 + 覆盖 future 句柄"会让旧任务变成无人等待的孤儿,
        // 继续解引用本对象成员 detailCancelPtr（窗口销毁后即 UAF）
        if (!m_detailCsvFuture.isFinished()) {
            m_detailCsvCancel.storeRelease(1);
            m_detailCsvFuture.waitForFinished();
        }
        m_detailCsvCancel.storeRelease(0);
        auto *detailCancelPtr = &m_detailCsvCancel;
        m_detailCsvFuture = QtConcurrent::run(
            [safeThis, snapshot, dp, dir, baseName, detailCancelPtr]() {
            if (!safeThis) return;
            QStringList errors;
            const int total = snapshot.size();
            for (int i = 0; i < total; ++i) {
                if (detailCancelPtr->loadAcquire()) break;  // 协作取消: closeEvent 置位后提前退出
                const DetailPassRef &p = snapshot[i];
                if (p.tleLine1.empty() || p.tleLine2.empty()) continue;
                // 文件编号 = 表格行号（下标+1）: 用户可在界面/主表里一一对应
                const int passNo = (p.row >= 0 ? p.row : i) + 1;
                // 每颗卫星用各自过境携带的 TLE；其余参数与页签同源
                DetailPropagationParams passParams = dp;
                passParams.tleLine1 = p.tleLine1;
                passParams.tleLine2 = p.tleLine2;
                QString detailPath = QString("%1/%2_detail_%3.csv")
                    .arg(dir).arg(baseName).arg(passNo);
                QString err = writePassDetailCsv(p, passParams, detailCancelPtr, detailPath);
                if (!err.isEmpty() && !detailCancelPtr->loadAcquire())
                    errors.append(QString("过境%1: %2").arg(passNo).arg(err));
                // 逐秒重传播比原实现（15 s 采样直写）耗时更长, 状态栏给出进度
                if (!detailCancelPtr->loadAcquire()) {
                    QMetaObject::invokeMethod(safeThis, [safeThis, i, total, passNo]() {
                        if (!safeThis || safeThis->m_isClosing) return;
                        safeThis->showStatusDedup(
                            QString("逐秒详情导出: %1/%2（过境 #%3）")
                                .arg(i + 1).arg(total).arg(passNo));
                    }, Qt::QueuedConnection);
                }
            }
            if (!errors.isEmpty() && !detailCancelPtr->loadAcquire()) {
                QString msg = QString("详情CSV导出失败 (%1 个):\n%2")
                    .arg(errors.size()).arg(errors.join("\n"));
                QMetaObject::invokeMethod(safeThis, [safeThis, msg]() {
                    if (!safeThis || safeThis->m_isClosing) return;
                    safeThis->showStatusDedup(msg);
                }, Qt::QueuedConnection);
            }
        });
    }
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

QVector<int> MainWindow::selectedPassRows() const
{
    QVector<int> rows;
    if (m_isClosing || !m_passTable || !m_passTable->selectionModel()) return rows;
    const QModelIndexList sel = m_passTable->selectionModel()->selectedRows();
    rows.reserve(sel.size());
    for (const QModelIndex &ix : sel)
        if (ix.isValid()) rows.append(ix.row());
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;   // 空 = 无选中 → 调用方按"全部"处理
}

bool MainWindow::startExport(ExportKind kind, const QString &filePath,
                             QVector<int> *rowsOut, QString *scopeOut)
{
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return false;
    }
    
    // 使用原子操作检查导出锁
    if (m_exportLock.fetchAndAddAcquire(1) != 0) {
        // 如果已经有导出在进行，提示用户并返回
        m_exportLock.fetchAndAddRelease(-1);
        showStatusDedup(QStringLiteral("导出正在进行中，请稍候"));
        return false;
    }
    
    // 计算/下载在途时禁止导出（入口按钮已同步禁用, 此处双保险）:
    // 导出线程直接读共享过境指针, 要求数据在导出期间不可变。
    // 不能静默 return: 一旦进度条因取消路径残留（见 applyFilterAndRefresh 的
    // 显式复位）, 用户会看到"按钮能点但完全没反应"
    if (m_progressBar->isVisible()) {
        m_exportLock.fetchAndAddRelease(-1);
        showStatusDedup("导出未执行: 计算/下载进行中，请稍候或先取消");
        return false;
    }

    // 导出范围: 过境列表中有选中行 → **只导出选中的过境**（及其逐秒详情）;
    // 无选中行 → 全部（过滤后的）过境，保持旧行为。
    // rowsOut 回传下标供详情文件命名（下标+1 = 表格行号，用户可一一对应）
    const QVector<int> selRows = selectedPassRows();

    // 锁定一切数据变更入口（清除/计算/下载/取消/导出）:
    // 此后 onResultsReady / onClear 均不可达, m_currentPasses 不可变,
    // 写盘线程解引用共享指针无悬垂风险, 也无需深拷贝快照（零主线程开销）
    setExportEnabled(false);
    m_clearBtn->setEnabled(false);
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);

    // 入口恢复（范围为空等早退路径必须成对调用，否则导出入口永久禁用）
    auto restoreEntryUi = [this]() {
        setExportEnabled(true);
        m_clearBtn->setEnabled(true);
        m_predictBtn->setEnabled(true);
        m_batchImportBtn->setEnabled(true);
        m_onlineUpdateBtn->setEnabled(true);
        m_cancelBtn->setEnabled(false);
    };

    if (!m_exportWatcher) {
        m_exportWatcher = std::make_unique<QFutureWatcher<QString>>(this);
        connect(m_exportWatcher.get(), &QFutureWatcher<QString>::finished,
                this, &MainWindow::onExportFinished);
    }
    
    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 指针向量 COW 浅拷贝（8B/项, 微秒级）: 即使主线程重建 m_filteredPasses
    // 也不影响本线程的只读视图; 过境对象本身因入口锁定不可变
    QVector<const PassEvent *> src;
    QVector<int> usedRows;
    const int total = m_filteredPasses.size();
    if (selRows.isEmpty()) {
        src = m_filteredPasses;
        usedRows.reserve(total);
        for (int i = 0; i < total; ++i) usedRows.append(i);
    } else {
        src.reserve(selRows.size());
        usedRows.reserve(selRows.size());
        for (int r : selRows) {
            if (r < 0 || r >= total) continue;   // 选中行已失效（结果被替换）
            src.append(m_filteredPasses[r]);
            usedRows.append(r);
        }
    }
    const double taskLat = m_taskLatDeg;
    const double taskLon = m_taskLonDeg;
    
    // 释放数据锁
    dataLocker.unlock();

    if (src.isEmpty()) {
        m_exportLock.fetchAndAddRelease(-1);
        restoreEntryUi();
        showStatusDedup(QStringLiteral("导出未执行: 没有可导出的过境"));
        return false;
    }

    if (rowsOut)  *rowsOut  = usedRows;
    if (scopeOut) {
        *scopeOut = selRows.isEmpty()
            ? QString("未选择行 → 导出全部 %1 个过境").arg(src.size())
            : QString("仅导出表格中选中的 %1 个过境").arg(src.size());
        showStatusDedup(*scopeOut);
    }
    
    // 使用弱指针模式，确保在导出过程中对象被销毁时能够安全处理
    // 导出函数支持协作式取消: closeEvent 置位 m_exportCancel 后写操作
    // 在下一过境边界处提前退出，避免阻塞关闭流程
    m_exportCancel.storeRelease(0);
    auto *cancelPtr = &m_exportCancel;
    m_exportWatcher->setFuture(
        QtConcurrent::run([kind, filePath, taskLat, taskLon, src, cancelPtr]() -> QString {
            switch (kind) {
            case ExportKind::Csv:  return writePassesCsv(src, filePath, cancelPtr);
            case ExportKind::Kml:  return writePassesKml(src, filePath, taskLat, taskLon, cancelPtr);
            }
            return QString();
        }));
    return true;   // 主导出已启动（m_exportLock 持有至 onExportFinished）
}

void MainWindow::onExportFinished()
{
    // 检查对象是否仍然有效
    if (m_isClosing) {
        m_exportLock.fetchAndAddRelease(-1);   // 早退也必须释放（配对缺口会永久禁用导出）
        return;
    }
    
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
    // result() 会重抛工作线程里被 QtConcurrent 捕获的异常（磁盘/内存类），
    // 槽内必须捕获, 否则异常穿出事件循环直接 terminate
    QString err;
    try {
        err = m_exportWatcher->result();
    } catch (const std::exception &e) {
        err = QString("导出线程异常: %1").arg(QString::fromUtf8(e.what()));
    } catch (...) {
        err = QStringLiteral("导出线程异常（未知类型）");
    }
    if (!err.isEmpty())
        QMessageBox::warning(this, "导出失败", err);
}

// ============================================================================
// 构造/析构
// ============================================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
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
    m_isClosing.store(true);

    // 在析构前断开所有信号槽连接，防止在对象销毁后有信号触发
    disconnectAllConnections();

    // 析构路径兜底: closeEvent 并非必经（父对象销毁 / 会话结束 / 直接 delete）。
    // disconnectAllConnections 只等了逐秒详情 watcher, 这里补齐导出与
    // detail CSV 两个 QtConcurrent 任务的等待——它们捕获了 MainWindow 的成员
    // (m_exportCancel / m_detailCsvCancel), 不等就可能在工作线程里访问已析构成员。
    // 判据用 !isFinished(): 任务"已入队未开跑"时 isRunning() 为 false（会漏等）
    if (m_exportWatcher && !m_exportWatcher->isFinished()) {
        m_exportCancel.storeRelease(1);
        m_exportWatcher->waitForFinished();
    }
    if (!m_detailCsvFuture.isFinished()) {
        m_detailCsvCancel.storeRelease(1);
        m_detailCsvFuture.waitForFinished();
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
    if (m_isClosing) return;

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
            Q_UNUSED(error);
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
    if (m_isClosing) return;

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
    if (m_isClosing) return;

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
        // 批量模式下不弹模态对话框（千颗级批量会逐条刷屏），仅状态栏提示
        if (m_batchMode) {
            showStatusDedup(QString("网络错误: %1").arg(error.message));
            return;
        }
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
        // 批量模式下不弹模态询问框（与上方 CRITICAL 弹窗同一防刷屏门控:
        // 千颗级批量会连弹等量模态框卡死界面）, 仅状态栏提示
        if (m_batchMode) {
            showStatusDedup(QString("配置错误: %1").arg(error.message));
            return;
        }
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
        // 文件IO错误: 临时禁用导出（10秒后自动恢复），避免永久锁定
        setExportEnabled(false);
        QTimer::singleShot(10000, this, [this]() {
            if (!m_isClosing) setExportEnabled(true);
        });
    }
}

void MainWindow::retryNetworkOperation(const ErrorInfo &info)
{
    Q_UNUSED(info);   // Release 下仅 QT_DEBUG 分支使用
    // 重试网络操作: 重新触发当前 TLE 组的在线下载（常规路径, 允许复用缓存）
    //（NETWORK_ERROR 当前生产无触发路径——createNetworkError 无调用者,
    // 本函数为未来网络链路复用预留）
    
    #ifdef QT_DEBUG
    qDebug() << "Retrying network operation for error:" << info.message;
    #endif
    
    // 显示重试进度
    setStatusText(m_statusModel, "正在重试网络操作...");
    
    forceFetchTle(false);
}

void MainWindow::resetToDefaultConfig()
{
    // 重置为默认配置的逻辑
    // 这里可以重置所有的UI控件到默认值
    
    #ifdef QT_DEBUG
    qDebug() << "Resetting to default configuration";
    #endif
    
    // 重置观测站位置（与 setupUI 默认值一致）
    m_latSpin->setValue(33.0);
    m_lonSpin->setValue(103.0);
    m_altSpin->setValue(0.05);     // 50米海拔
    
    // 重置其他配置
    m_maskSpin->setValue(10.0);    // 10度掩星角
    // 时间窗口以**同一时刻**为基准, 保证刚好 24 h（两次 currentDateTimeUtc()
    // 之间会差几十微秒, hoursAhead 会略小于 24）
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        m_startTimeEdit->setDateTime(now);
        m_endTimeEdit->setDateTime(now.addSecs(24 * 3600));
    }
    
    // 重置暗天阈值
    m_darkSkyCombo->setCurrentIndex(0);

    m_gravModelCombo->setCurrentIndex(0);
    // 复位导出/筛选开关: 重置后"仅显示可见过境"仍生效会让表格少显示过境、
    // "含逐秒详情"仍勾选会让下次导出意外写出大量 detail 文件
    if (m_visibleOnlyCheck)    m_visibleOnlyCheck->setChecked(false);
    if (m_exportDetailCheck)   m_exportDetailCheck->setChecked(false);

    setStatusText(m_statusModel, "配置已重置为默认值");
}

#include "MainWindow.moc"
