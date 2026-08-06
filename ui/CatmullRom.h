#ifndef UI_CATMULLROM_H
#define UI_CATMULLROM_H
// =============================================================================
// CatmullRom.h — 采样点平滑曲线工具（Catmull-Rom → 三次贝塞尔）
// 均匀参数 Catmull-Rom 经过所有控制点，用相邻四点的中点切线构造贝塞尔段:
//   段 i: P0=pi, C1=pi+(p(i+1)−p(i−1))/6, C2=p(i+1)−(p(i+2)−pi)/6, P3=p(i+1)
// 端点处边界点复制（p−1=p0, pn=p(n−1)），曲线保持插值且 C1 连续
// =============================================================================
#include <QPainterPath>
#include <QPointF>
#include <QVector>

namespace CatmullRom {

// 将点列转成经过每个点的平滑 QPainterPath（不闭合成环）
// pts 少于 2 个时返回仅含 moveTo 的空路径
inline QPainterPath path(const QVector<QPointF> &pts)
{
    QPainterPath p;
    const int n = pts.size();
    if (n == 0) return p;
    p.moveTo(pts[0]);
    if (n < 2) return p;

    auto at = [&pts, n](int i) -> QPointF {
        if (i < 0)   return pts[0];
        if (i >= n)  return pts[n - 1];
        return pts[i];
    };

    for (int i = 0; i < n - 1; ++i) {
        const QPointF p0 = at(i - 1);
        const QPointF p1 = pts[i];
        const QPointF p2 = pts[i + 1];
        const QPointF p3 = at(i + 2);
        p.cubicTo(p1 + (p2 - p0) / 6.0,
                  p2 - (p3 - p1) / 6.0,
                  p2);
    }
    return p;
}

} // namespace CatmullRom

#endif // UI_CATMULLROM_H
