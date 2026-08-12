#ifndef UI_DOPPLERPLOTWIDGET_H
#define UI_DOPPLERPLOTWIDGET_H
// =============================================================================
// DopplerPlotWidget.h — 多普勒全程曲线图
// 数据: PassEvent::samples[].dopplerKHz（过境弧段等步长采样）
// X 轴: 相对各过境入境时刻的时间（秒）; Y 轴: 多普勒频移（kHz）
// =============================================================================
#include <QWidget>
#include <QVector>
#include "core/models.h"

class DopplerPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit DopplerPlotWidget(QWidget *parent = nullptr);

    void setPasses(const QVector<PassEvent> &passes);
    void setSelectedPass(int index);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<PassEvent> m_passes;
    int m_selectedIndex = -1;
};

#endif // UI_DOPPLERPLOTWIDGET_H