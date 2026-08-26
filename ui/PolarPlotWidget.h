#ifndef UI_POLARPLOTWIDGET_H
#define UI_POLARPLOTWIDGET_H
// =============================================================================
// PolarPlotWidget.h — 极坐标仰角-方位角图（雷达图）
// QPainter 纯绘制: 同心圆=仰角, 径向线=方位角
// =============================================================================
#include <QWidget>
#include <QVector>
#include <QPainterPath>
#include "core/models.h"

class PolarPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit PolarPlotWidget(QWidget *parent = nullptr);

    // 共享数据指针（指向 MainWindow 持有的过境集合, 不深拷贝采样向量）
    void setPasses(const QVector<const PassEvent *> &passes);
    void setSelectedPass(int index);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<const PassEvent *> m_passes;
    int m_selectedIndex = -1;

    // 将仰角/方位角映射到绘图区域的笛卡尔坐标
    QPointF polarToScreen(double azDeg, double elDeg, double cx, double cy, double radius) const;

    // 单条过境的曲线路径构建（供缓存复用）
    QPainterPath buildTrackPath(const PassEvent &p,
                                double cx, double cy, double radius) const;

    // 曲线路径缓存: 每帧为全部过境重建 Catmull-Rom 路径（批量数百条曲线 ×
    // 数十采样点）在选中切换/resize 重绘风暴下开销明显——以「指针集合 + 尺寸」
    // 为版本键（路径只依赖数据与屏幕几何）, 未变时复用
    QVector<QPainterPath> m_pathCache;
    QVector<const PassEvent *> m_pathKeyPasses;
    QSize m_pathKeySize;
};

#endif // UI_POLARPLOTWIDGET_H
