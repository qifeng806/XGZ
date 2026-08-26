#ifndef UI_GROUNDTRACKWIDGET_H
#define UI_GROUNDTRACKWIDGET_H
// =============================================================================
// GroundTrackWidget.h — 二维地面轨迹图
// 等矩形投影（横轴=经度，纵轴=纬度），QPainter 纯绘制
// =============================================================================
#include <QWidget>
#include <QVector>
#include <QPolygonF>
#include <QPainterPath>
#include "core/models.h"

class GroundTrackWidget : public QWidget {
    Q_OBJECT
public:
    explicit GroundTrackWidget(QWidget *parent = nullptr);

    // 共享数据指针（指向 MainWindow 持有的过境集合, 不深拷贝采样向量）
    void setPasses(const QVector<const PassEvent *> &passes,
                   double observerLatDeg, double observerLonDeg);
    void setSelectedPass(int index);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<const PassEvent *> m_passes;
    int m_selectedIndex = -1;
    double m_obsLat = 0.0, m_obsLon = 0.0;

    // 真实大陆轮廓（Natural Earth 110m，经度在 [-180,180] 内，无需断片）
    QVector<QPolygonF> m_landPolygons;   // 点 = (latDeg, lonDeg)
    bool m_landLoaded = false;           // 首次 paint 惰性加载标记
    // 投影缓存: 每帧重投影数百个多边形在重绘风暴（resize/遮挡）下开销明显,
    // 尺寸不变时复用缓存结果
    QVector<QPolygonF> m_projectedLand;
    QSize m_projectedSize;               // 投影时的 widget 尺寸（变化即失效）
    qreal m_projectedDpr = 0.0;          // 投影时的设备像素比（跨不同缩放显示器拖动时失效重建）

    // 轨迹路径缓存: 与陆地投影缓存同理, 以「指针集合 + 尺寸」为版本键
    QVector<QPainterPath> m_trackPathCache;
    QVector<const PassEvent *> m_pathKeyPasses;
    QSize m_pathKeySize;

    bool loadLandData();                 // 从 :/land110m.txt 加载一次
    QPointF geoToScreen(double latDeg, double lonDeg,
                        double x0, double y0, double sx, double sy) const;

    // 单条过境的轨迹路径构建（供缓存复用）
    QPainterPath buildTrackPath(const PassEvent &p,
                                double x0, double y0, double sx, double sy) const;
};

#endif // UI_GROUNDTRACKWIDGET_H
