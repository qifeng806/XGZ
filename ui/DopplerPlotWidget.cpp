// =============================================================================
// DopplerPlotWidget.cpp — 多普勒全程曲线实现
// 每条过境一条折线: 相对 AOS 的时间(秒) × 多普勒频移(kHz)
// 选中过境高亮（白线加粗），其余曲线半透明便于区分
// =============================================================================
#include "DopplerPlotWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
// 曲线配色（与极坐标图风格一致，深色底）
const QColor kPalette[] = {
    QColor(79, 195, 247), QColor(255, 183, 77), QColor(129, 199, 132),
    QColor(229, 115, 115), QColor(186, 104, 200), QColor(77, 182, 172),
    QColor(255, 241, 118), QColor(161, 136, 127),
};

// 判断一次过境是否含多普勒曲线数据
bool hasDopplerData(const PassEvent &p)
{
    for (const auto &s : p.samples)
        if (s.dopplerKHz != 0.0) return true;
    return false;
}

// 某条曲线的 (X=相对AOS秒, Y=kHz) 点集
QVector<QPointF> curvePoints(const PassEvent &p)
{
    QVector<QPointF> pts;
    for (const auto &s : p.samples)
        pts.append(QPointF((s.jdMs - p.aosJdMs) / 1000.0, s.dopplerKHz));
    return pts;
}
} // namespace

DopplerPlotWidget::DopplerPlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 260);
}

void DopplerPlotWidget::setPasses(const QVector<const PassEvent *> &passes)
{
    m_passes = passes;
    // 保留原选中行: 过滤切换后若选中越界则钳到末尾, 而非强制回到第 0 行
    if (passes.isEmpty())
        m_selectedIndex = -1;
    else if (m_selectedIndex >= passes.size())
        m_selectedIndex = passes.size() - 1;
    update();
}

void DopplerPlotWidget::setSelectedPass(int index)
{
    if (index >= 0 && index < m_passes.size()) {
        m_selectedIndex = index;
        update();
    }
}

void DopplerPlotWidget::clear()
{
    m_passes.clear();
    m_selectedIndex = -1;
    update();
}

// ============================================================================
// 绘制
// ============================================================================
void DopplerPlotWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(38, 42, 54));

    // 收集含数据曲线的过境
    QVector<int> curveIdx;   // m_passes 中的下标
    for (int i = 0; i < m_passes.size(); ++i)
        if (hasDopplerData(*m_passes[i])) curveIdx.append(i);
    if (curveIdx.isEmpty()) {
        painter.setPen(QColor(175, 182, 195));
        painter.drawText(rect(), Qt::AlignCenter, "无多普勒数据\n（载频为零或采样未启用）");
        return;
    }

    // 点集只计算一次（范围扫描与绘制共用），避免每条曲线重复遍历/分配
    QVector<QVector<QPointF>> curvePts(curveIdx.size());
    for (int i = 0; i < curveIdx.size(); ++i)
        curvePts[i] = curvePoints(*m_passes[curveIdx[i]]);

    // 数据范围: X = 0..maxDuration, Y = [min, max]
    double xMax = 0.0, yMin = 0.0, yMax = 0.0;
    for (const auto &pts : curvePts) {
        for (const auto &pt : pts) {
            xMax = std::max(xMax, pt.x());
            yMin = std::min(yMin, pt.y());
            yMax = std::max(yMax, pt.y());
        }
    }
    if (yMax - yMin < 1e-9) { yMin -= 0.5; yMax += 0.5; }
    const double yPad = (yMax - yMin) * 0.08;
    yMin -= yPad; yMax += yPad;
    if (xMax <= 0.0) xMax = 1.0;   // 除零防护: 全部数据点 x=0 的退化曲线
    const double xPad = xMax * 0.02;
    xMax += xPad;

    const QRectF plotArea(55, 20, width() - 70, height() - 70);
    auto mapX = [&](double x) {
        return plotArea.left() + (x / xMax) * plotArea.width();
    };
    auto mapY = [&](double y) {
        return plotArea.bottom() - ((y - yMin) / (yMax - yMin)) * plotArea.height();
    };

    // 网格与坐标轴
    painter.setPen(QPen(QColor(105, 113, 130), 1));
    const int yTicks = 5;
    for (int t = 0; t <= yTicks; ++t) {
        double y = yMin + (yMax - yMin) * t / yTicks;
        double sy = mapY(y);
        painter.drawLine(QPointF(plotArea.left(), sy), QPointF(plotArea.right(), sy));
        painter.setPen(QColor(195, 202, 215));
        painter.drawText(QRectF(0, sy - 8, plotArea.left() - 5, 16),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(y, 'f', 1));
        painter.setPen(QPen(QColor(105, 113, 130), 1));
    }
    const int xTicks = 5;
    for (int t = 0; t <= xTicks; ++t) {
        double x = xMax * t / xTicks;
        double sx = mapX(x);
        painter.drawLine(QPointF(sx, plotArea.top()), QPointF(sx, plotArea.bottom()));
        painter.setPen(QColor(195, 202, 215));
        painter.drawText(QRectF(sx - 25, plotArea.bottom() + 6, 50, 16),
                         Qt::AlignCenter, QString::number(x, 'f', 0));
        painter.setPen(QPen(QColor(105, 113, 130), 1));
    }
    painter.setPen(QColor(215, 222, 235));
    painter.drawText(QRectF(plotArea.left(), 2, plotArea.width(), 14),
                     Qt::AlignHCenter, "多普勒频移（kHz，相对过境入境时刻）");

    // 零频参考线（中心频率 → 多普勒偏移 0）
    if (yMin < 0.0 && yMax > 0.0) {
        painter.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DashLine));
        painter.drawLine(QPointF(plotArea.left(), mapY(0.0)),
                         QPointF(plotArea.right(), mapY(0.0)));
    }

    // 曲线
    const bool hasSel = (m_selectedIndex >= 0 && m_selectedIndex < m_passes.size()
                         && hasDopplerData(*m_passes[m_selectedIndex]));
    for (int i = 0; i < curveIdx.size(); ++i) {
        const int idx = curveIdx[i];
        const bool isSel = (idx == m_selectedIndex);
        QColor col = kPalette[i % (sizeof(kPalette) / sizeof(kPalette[0]))];
        if (hasSel && !isSel) {
            col.setAlpha(150);
            painter.setPen(QPen(col, 1.5));
        } else if (isSel) {
            painter.setPen(QPen(QColor(255, 255, 255), 2.6));
        } else {
            painter.setPen(QPen(col, 1.8));
        }

        const QVector<QPointF> &pts = curvePts[i];
        QPolygonF poly;
        for (const auto &pt : pts)
            poly.append(QPointF(mapX(pt.x()), mapY(pt.y())));
        if (!poly.isEmpty()) painter.drawPolyline(poly);

        // 选中过境: 图例 + 极值标注
        if (isSel) {
            double yPeak = pts.isEmpty() ? 0.0 : pts[0].y();
            for (const auto &pt : pts) {
                if (std::abs(pt.y()) > std::abs(yPeak)) yPeak = pt.y();
            }
            painter.setPen(QColor(220, 220, 220));
            painter.drawText(QRectF(plotArea.left(), plotArea.bottom() + 22,
                                    plotArea.width(), 16),
                             Qt::AlignHCenter,
                             QString("%1   峰值频移: %2 kHz")
                                 .arg(QString::fromStdString(m_passes[idx]->satelliteName))
                                 .arg(yPeak, 0, 'f', 2));
        }
    }
}