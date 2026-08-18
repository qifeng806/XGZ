#ifndef UI_DOPPLERPLOTWIDGET_H
#define UI_DOPPLERPLOTWIDGET_H
// =============================================================================
// DopplerPlotWidget.h — 多普勒全程曲线图
// 数据: PassEvent::samples[].dopplerKHz（过境弧段等步长采样）
// X 轴: 相对各过境入境时刻的时间（秒）; Y 轴: 多普勒频移（kHz）
// =============================================================================
#include <QWidget>
#include <QVector>
#include <QPointF>
#include "core/models.h"

class DopplerPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit DopplerPlotWidget(QWidget *parent = nullptr);

    // 共享数据指针（指向 MainWindow 持有的过境集合, 不深拷贝采样向量）
    void setPasses(const QVector<const PassEvent *> &passes);
    void setSelectedPass(int index);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<const PassEvent *> m_passes;
    int m_selectedIndex = -1;

    // 数据点集缓存: paintEvent 每帧重建（每条曲线几百点 × 批量数百曲线 =
    // 每帧数千次分配）; 以「指针集合 + 选中」为版本键, 数据未变时复用
    QVector<QVector<QPointF>> m_curvePtsCache;
    QVector<const PassEvent *> m_cacheKeyPasses;
    int m_cacheKeySelection = -1;
};

#endif // UI_DOPPLERPLOTWIDGET_H