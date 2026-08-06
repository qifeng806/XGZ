#ifndef UI_POLARPLOTWIDGET_H
#define UI_POLARPLOTWIDGET_H
// =============================================================================
// PolarPlotWidget.h — 极坐标仰角-方位角图（雷达图）
// QPainter 纯绘制: 同心圆=仰角, 径向线=方位角
// =============================================================================
#include <QWidget>
#include <QVector>
#include "core/models.h"

class PolarPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit PolarPlotWidget(QWidget *parent = nullptr);

    void setPasses(const QVector<PassEvent> &passes);
    void setSelectedPass(int index);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QVector<PassEvent> m_passes;
    int m_selectedIndex = -1;

    // 将仰角/方位角映射到绘图区域的笛卡尔坐标
    QPointF polarToScreen(double azDeg, double elDeg, double cx, double cy, double radius) const;

    // 计算鼠标位置对应的仰角和方位角
    void screenToPolar(const QPointF &pos, double &azDeg, double &elDeg,
                       double cx, double cy, double radius) const;
};

#endif // UI_POLARPLOTWIDGET_H
