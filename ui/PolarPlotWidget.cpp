// =============================================================================
// PolarPlotWidget.cpp — 极坐标图实现
// =============================================================================
#include "PolarPlotWidget.h"
#include "CatmullRom.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QToolTip>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PolarPlotWidget::PolarPlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(300, 300);
    setMouseTracking(true);
}

void PolarPlotWidget::setPasses(const QVector<const PassEvent *> &passes)
{
    m_passes = passes;
    // 保留原选中行: 过滤切换后若选中越界则钳到末尾, 而非强制回到第 0 行
    if (passes.isEmpty())
        m_selectedIndex = -1;
    else if (m_selectedIndex >= passes.size())
        m_selectedIndex = passes.size() - 1;
    update();
}

void PolarPlotWidget::setSelectedPass(int index)
{
    if (index >= 0 && index < m_passes.size()) {
        m_selectedIndex = index;
        update();
    }
}

void PolarPlotWidget::clear()
{
    m_passes.clear();
    m_selectedIndex = -1;
    update();
}

// ============================================================================
// 坐标映射: 仰角/方位角 → 屏幕笛卡尔坐标
// azDeg: 0=N, 90=E, 180=S, 270=W
// elDeg: 0=地平线, 90=天顶
// 圆心 = 天顶(90°), 最外圈 = 地平(0°)
// ============================================================================
QPointF PolarPlotWidget::polarToScreen(double azDeg, double elDeg,
                                        double cx, double cy, double radius) const
{
    // 将仰角映射为距圆心的距离：elDeg=90 → r=0, elDeg=0 → r=radius
    double r = radius * (1.0 - elDeg / 90.0);
    // 方位角: 北=0°，但 Qt 坐标系 Y 轴向下，所以需要转换
    // 地图: 北=上(270° in math), 东=右(0° in math)
    // 转换为标准数学坐标系: 0°=右, 90°=上
    double azRad = (90.0 - azDeg) * M_PI / 180.0;
    double x = cx + r * std::cos(azRad);
    double y = cy - r * std::sin(azRad);
    return QPointF(x, y);
}

void PolarPlotWidget::screenToPolar(const QPointF &pos, double &azDeg, double &elDeg,
                                     double cx, double cy, double radius) const
{
    double dx = pos.x() - cx;
    double dy = cy - pos.y();  // Y 轴翻转
    double r = std::sqrt(dx * dx + dy * dy);
    elDeg = 90.0 * (1.0 - std::min(r / radius, 1.0));
    double azRad = std::atan2(dy, dx);  // 数学坐标系: 0°=右
    azDeg = 90.0 - azRad * 180.0 / M_PI;
    if (azDeg < 0.0) azDeg += 360.0;
}

// ============================================================================
// 绘制
// ============================================================================
void PolarPlotWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    double w = width(), h = height();
    double cx = w / 2.0, cy = h / 2.0;
    double radius = std::min(w, h) / 2.0 - 40;

    // 背景（亮色系, 保证深色曲线清晰可见）
    painter.fillRect(rect(), QColor(38, 42, 54));
    painter.setPen(QPen(QColor(105, 113, 130), 1));

    // 同心圆（仰角环）
    for (int elRing : {90, 70, 50, 30, 10, 0}) {
        double r = radius * (1.0 - elRing / 90.0);
        if (elRing == 0)
            painter.setPen(QPen(QColor(135, 145, 165), 2));   // 地平圈加粗
        else
            painter.setPen(QPen(QColor(105, 113, 130), 1));
        painter.drawEllipse(QPointF(cx, cy), r, r);
    }

    // 径向线（方位角刻度）
    for (int az = 0; az < 360; az += 30) {
        double azRad = (90.0 - az) * M_PI / 180.0;
        double x = cx + radius * std::cos(azRad);
        double y = cy - radius * std::sin(azRad);
        bool cardinal = (az % 90 == 0);
        painter.setPen(QPen(cardinal ? QColor(135, 145, 165)
                                     : QColor(88, 95, 112), cardinal ? 2 : 1));
        painter.drawLine(QPointF(cx, cy), QPointF(x, y));
    }

    // 绘制各过境的仰角-方位角曲线（全部以半透明绘制）
    QVector<QColor> passColors = {
        QColor(110, 215, 255), QColor(255, 195, 90),
        QColor(150, 255, 150), QColor(255, 145, 135),
        QColor(215, 175, 255), QColor(255, 255, 135)
    };

    for (int pi = 0; pi < m_passes.size(); ++pi) {
        const PassEvent *p = m_passes[pi];
        bool isSelected = (pi == m_selectedIndex);
        QColor col = passColors[pi % passColors.size()];
        if (!isSelected) col.setAlpha(170);

        QPainterPath path;
        // 特征点（AOS/TCA/LOS 标记与贝塞尔回退共用）
        QPointF aosP = polarToScreen(p->aosLook.azDeg, p->aosLook.elevDeg, cx, cy, radius);
        QPointF tcaP = polarToScreen(p->tcaLook.azDeg, p->maxElevDeg, cx, cy, radius);
        QPointF losP = polarToScreen(p->losLook.azDeg, p->losLook.elevDeg, cx, cy, radius);

        if (p->samples.size() >= 2) {
            // 真实弧段: Catmull-Rom 平滑曲线（经过所有采样点）
            // 方位角跨 0°/360° 处断开分段平滑，避免整圈环绕伪影
            QVector<QPointF> seg;
            for (size_t k = 0; k < p->samples.size(); ++k) {
                const auto &s = p->samples[k];
                if (!seg.isEmpty()) {
                    double daz = std::abs(s.look.azDeg - p->samples[k - 1].look.azDeg);
                    if (daz > 180.0) {    // 跨 0°/360°: 断开
                        path.addPath(CatmullRom::path(seg));
                        seg.clear();
                    }
                }
                seg.append(polarToScreen(s.look.azDeg, s.look.elevDeg, cx, cy, radius));
            }
            path.addPath(CatmullRom::path(seg));
        } else {
            // 回退: AOS → TCA → LOS 二次贝塞尔近似（无采样数据时）
            path.moveTo(aosP);
            path.quadTo(tcaP, losP);
        }

        if (isSelected) {
            // 选中路径光晕（先宽后窄，突出主曲线）
            QColor glow = col;
            glow.setAlpha(70);
            painter.setPen(QPen(glow, 9));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }
        painter.setPen(QPen(col, isSelected ? 3.5 : 1.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        // 标注点
        if (isSelected) {
            painter.setPen(QPen(QColor(20, 24, 32), 1));
            painter.setBrush(QColor(0, 255, 200));
            painter.drawEllipse(aosP, 5, 5);
            painter.setBrush(QColor(255, 255, 0));
            painter.drawEllipse(tcaP, 5, 5);
            painter.setBrush(QColor(255, 90, 90));
            painter.drawEllipse(losP, 5, 5);

            // 标签
            painter.setPen(Qt::white);
            painter.drawText(aosP + QPointF(9, -9), "AOS");
            painter.drawText(tcaP + QPointF(9, -9), "TCA");
            painter.drawText(losP + QPointF(9, -9), "LOS");
        }
    }

    // 刻度标签在曲线之后绘制, 保证永不被过境曲线遮挡
    for (int elRing : {90, 70, 50, 30, 10, 0}) {
        double r = radius * (1.0 - elRing / 90.0);
        painter.setPen(QColor(195, 202, 215));
        QString txt = QString::number(elRing) + "°";
        painter.drawText(QRectF(cx + 6, cy - r - 8, 40, 16),
                         Qt::AlignLeft | Qt::AlignVCenter, txt);
    }
    for (int az = 0; az < 360; az += 30) {
        double azRad = (90.0 - az) * M_PI / 180.0;
        QPointF labelPos(cx + (radius + 18) * std::cos(azRad),
                         cy - (radius + 18) * std::sin(azRad));
        painter.setPen(QColor(205, 212, 225));
        QString label;
        if (az == 0) label = "N";
        else if (az == 90) label = "E";
        else if (az == 180) label = "S";
        else if (az == 270) label = "W";
        else label = QString::number(az) + "°";
        painter.drawText(QRectF(labelPos.x() - 20, labelPos.y() - 10, 40, 20),
                         Qt::AlignCenter, label);
    }

    // 如果没有过境，显示提示
    if (m_passes.isEmpty()) {
        painter.setPen(QColor(175, 182, 195));
        painter.drawText(QRectF(cx - 100, cy - 15, 200, 30),
                         Qt::AlignCenter, "暂无过境数据");
    }
}

void PolarPlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    // 预留: 显示 tooltip
}
