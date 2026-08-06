#ifndef UI_GROUNDTRACKWIDGET_H
#define UI_GROUNDTRACKWIDGET_H
// =============================================================================
// GroundTrackWidget.h — 二维地面轨迹图
// 等矩形投影（横轴=经度，纵轴=纬度），QPainter 纯绘制
// =============================================================================
#include <QWidget>
#include <QVector>
#include "core/models.h"

class GroundTrackWidget : public QWidget {
    Q_OBJECT
public:
    explicit GroundTrackWidget(QWidget *parent = nullptr);

    void setPasses(const QVector<PassEvent> &passes,
                   double observerLatDeg, double observerLonDeg);
    void setSelectedPass(int index);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<PassEvent> m_passes;
    int m_selectedIndex = -1;
    double m_obsLat = 0.0, m_obsLon = 0.0;

    QPointF geoToScreen(double latDeg, double lonDeg,
                        double x0, double y0, double sx, double sy) const;
};

#endif // UI_GROUNDTRACKWIDGET_H
