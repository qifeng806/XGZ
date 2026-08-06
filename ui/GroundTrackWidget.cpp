// =============================================================================
// GroundTrackWidget.cpp — 地面轨迹图实现
// =============================================================================
#include "GroundTrackWidget.h"
#include "CatmullRom.h"
#include <QPainter>
#include <QPainterPath>
#include <QFile>
#include <QTextStream>
#include <cmath>

GroundTrackWidget::GroundTrackWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 250);
    loadLandData();
}

// ============================================================================
// loadLandData — 从 qrc 资源加载 Natural Earth 110m 陆地多边形
// 格式: "P 名称" 开始一个多边形, 随后每行 "lat lon"（度，一位小数即 ~11km
// 精度，适合 110m 比例尺），"E" 结束。所有多边形纬度跨度均 < 180°，
// 直接绘制无需跨经线断片
// ============================================================================
bool GroundTrackWidget::loadLandData()
{
    QFile f(":/land110m.txt");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    QPolygonF cur;
    QString line;
    while (!(line = in.readLine()).isNull()) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('P')) {
            if (!cur.isEmpty()) m_landPolygons.append(cur);
            cur.clear();
        } else if (line.startsWith('E')) {
            if (!cur.isEmpty()) m_landPolygons.append(cur);
            cur.clear();
        } else {
            const QStringList parts = line.split(' ');
            if (parts.size() >= 2) {
                bool ok1 = false, ok2 = false;
                const double lat = parts[0].toDouble(&ok1);
                const double lon = parts[1].toDouble(&ok2);
                if (ok1 && ok2) cur.append(QPointF(lat, lon));
            }
        }
    }
    if (!cur.isEmpty()) m_landPolygons.append(cur);
    return !m_landPolygons.isEmpty();
}

void GroundTrackWidget::setPasses(const QVector<PassEvent> &passes,
                                   double observerLatDeg, double observerLonDeg)
{
    m_passes = passes;
    m_obsLat = observerLatDeg;
    m_obsLon = observerLonDeg;
    m_selectedIndex = (passes.isEmpty()) ? -1 : 0;
    update();
}

void GroundTrackWidget::setSelectedPass(int index)
{
    if (index >= 0 && index < m_passes.size()) {
        m_selectedIndex = index;
        update();
    }
}

void GroundTrackWidget::clear()
{
    m_passes.clear();
    m_selectedIndex = -1;
    update();
}

QPointF GroundTrackWidget::geoToScreen(double latDeg, double lonDeg,
                                        double x0, double y0, double sx, double sy) const
{
    // 等矩形投影: lat → y (北=上=小), lon → x (东=右=大)
    double x = x0 + (lonDeg + 180.0) * sx;
    double y = y0 + (90.0 - latDeg) * sy;
    return QPointF(x, y);
}

void GroundTrackWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    double w = width(), h = height();
    double margin = 40;
    double x0 = margin, y0 = margin;
    double plotW = w - 2 * margin, plotH = h - 2 * margin;
    double sx = plotW / 360.0;   // 经度 → 像素
    double sy = plotH / 180.0;   // 纬度 → 像素

    // 背景
    painter.fillRect(rect(), QColor(25, 30, 40));

    // 网格线（每 30°）
    painter.setPen(QPen(QColor(50, 55, 65), 1));
    for (int lat = -90; lat <= 90; lat += 30) {
        double y = y0 + (90.0 - lat) * sy;
        painter.drawLine(QPointF(x0, y), QPointF(x0 + plotW, y));
    }
    for (int lon = -180; lon <= 180; lon += 30) {
        double x = x0 + (lon + 180.0) * sx;
        painter.drawLine(QPointF(x, y0), QPointF(x, y0 + plotH));
    }

    // 赤道加粗
    painter.setPen(QPen(QColor(70, 75, 85), 2));
    {
        double yEq = y0 + 90.0 * sy;
        painter.drawLine(QPointF(x0, yEq), QPointF(x0 + plotW, yEq));
    }

    // 经纬度标签
    painter.setPen(QColor(140, 140, 140));
    QFont labelFont = painter.font();
    labelFont.setPointSize(8);
    painter.setFont(labelFont);
    for (int lat = -60; lat <= 60; lat += 30) {
        double y = y0 + (90.0 - lat) * sy;
        painter.drawText(QPointF(x0 - 35, y + 4), QString::number(lat) + "°");
    }
    for (int lon = -180; lon <= 180; lon += 60) {
        double x = x0 + (lon + 180.0) * sx;
        painter.drawText(QPointF(x - 15, y0 + plotH + 15), QString::number(lon) + "°");
    }

    // 真实大陆轮廓（Natural Earth 110m，qrc 资源加载）
    // 先于过境轨迹绘制，保证轨迹线不被大陆遮挡
    painter.setPen(QPen(QColor(60, 65, 75), 1));
    painter.setBrush(QColor(35, 40, 50));
    for (const auto &poly : m_landPolygons) {
        if (poly.size() < 3) continue;
        QPolygonF screen;
        screen.reserve(poly.size());
        for (const QPointF &pt : poly)
            screen.append(geoToScreen(pt.x(), pt.y(), x0, y0, sx, sy));
        painter.drawPolygon(screen);
    }

    // 地面站标注
    {
        QPointF obsP = geoToScreen(m_obsLat, m_obsLon, x0, y0, sx, sy);
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(Qt::black);
        painter.drawRect(QRectF(obsP.x() - 5, obsP.y() - 5, 10, 10));
        painter.drawText(obsP + QPointF(8, -8), "站");
    }

    // 绘制过境地面轨迹
    QVector<QColor> trackColors = {
        QColor(80, 180, 255), QColor(255, 160, 60),
        QColor(100, 255, 100), QColor(255, 100, 100)
    };

    for (int pi = 0; pi < m_passes.size(); ++pi) {
        const auto &p = m_passes[pi];
        bool isSelected = (pi == m_selectedIndex);
        QColor col = trackColors[pi % trackColors.size()];
        if (!isSelected) col.setAlpha(60);

        QPainterPath trackPath;
        // 特征点（AOS/TCA/LOS 标记与贝塞尔回退共用）
        QPointF aosP = geoToScreen(p.aosSatGeo.latDeg, p.aosSatGeo.lonDeg, x0, y0, sx, sy);
        QPointF tcaP = geoToScreen(p.tcaSatGeo.latDeg, p.tcaSatGeo.lonDeg, x0, y0, sx, sy);
        QPointF losP = geoToScreen(p.losSatGeo.latDeg, p.losSatGeo.lonDeg, x0, y0, sx, sy);

        if (p.samples.size() >= 2) {
            // 真实地面轨迹: Catmull-Rom 平滑曲线（经过所有采样点）
            // 跨越 ±180° 经线处断开分段平滑，避免整幅横穿伪影
            auto screenPt = [&](const PassSample &s) {
                return geoToScreen(s.geo.latDeg, s.geo.lonDeg, x0, y0, sx, sy);
            };
            QVector<QPointF> seg;
            for (size_t k = 0; k < p.samples.size(); ++k) {
                if (!seg.isEmpty()) {
                    double dlon = std::abs(p.samples[k].geo.lonDeg
                                           - p.samples[k - 1].geo.lonDeg);
                    if (dlon > 180.0) {       // 跨经线: 断开
                        trackPath.addPath(CatmullRom::path(seg));
                        seg.clear();
                    }
                }
                seg.append(screenPt(p.samples[k]));
            }
            trackPath.addPath(CatmullRom::path(seg));
        } else {
            // 回退: AOS → TCA → LOS 二次贝塞尔近似（无采样数据时）
            trackPath.moveTo(aosP);
            trackPath.quadTo(tcaP, losP);
        }
        painter.setPen(QPen(col, isSelected ? 2 : 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(trackPath);

        if (isSelected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 255, 200));
            painter.drawEllipse(aosP, 5, 5);
            painter.setBrush(QColor(255, 255, 0));
            painter.drawEllipse(tcaP, 5, 5);
            painter.setBrush(QColor(255, 80, 80));
            painter.drawEllipse(losP, 5, 5);

            painter.setPen(Qt::white);
            painter.drawText(aosP + QPointF(8, -6), "AOS");
            painter.drawText(tcaP + QPointF(8, -6), "TCA");
            painter.drawText(losP + QPointF(8, -6), "LOS");
        }
    }

    // 如果没有过境，显示提示
    if (m_passes.isEmpty()) {
        painter.setPen(QColor(150, 150, 150));
        painter.setFont(QFont("Arial", 12));
        painter.drawText(QRectF(x0, y0, plotW, plotH),
                         Qt::AlignCenter, "暂无过境数据");
    }
}
