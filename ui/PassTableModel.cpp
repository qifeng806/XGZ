// =============================================================================
// PassTableModel.cpp — 过境列表懒加载模型实现
// 单元格文本/颜色/对齐/提示全部按角色惰性计算, 仅可见单元格被求值
// =============================================================================
#include "PassTableModel.h"
#include <QTimeZone>
#include <QDateTime>
#include <QBrush>

PassTableModel::PassTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void PassTableModel::setPasses(const QVector<const PassEvent *> &passes)
{
    {
        QMutexLocker locker(&m_dataMutex);

        // 指针集合未变（时区切换、同参数重算等）: 跳过全量 reset,
        // 保留视图滚动位置与当前选中（setTimeZoneOffset 已另行发通知）
        if (m_passes == passes) return;
        m_passes = passes;
    }

    // 必须在锁外发射 reset 信号: 视图会同步回调 data()/rowCount()
    // 再抢同一把非递归锁, 持锁 reset 会与 data() 死锁
    beginResetModel();
    endResetModel();
}

void PassTableModel::setTimeZoneOffset(int tzOffsetSec)
{
    QMutexLocker locker(&m_dataMutex);
    
    if (m_tzOffsetSec == tzOffsetSec) return;
    m_tzOffsetSec = tzOffsetSec;
    
    // 释放锁再发射信号，避免死锁
    locker.unlock();
    
    // 表头与时间列随之变化
    emit headerDataChanged(Qt::Horizontal, ColAos, ColLos);
    if (!m_passes.isEmpty())
        emit dataChanged(index(0, ColAos), index(rowCount() - 1, ColLos));
}

QString PassTableModel::tzLabel() const
{
    if (m_tzOffsetSec == 0) return QStringLiteral("UTC");
    return QStringLiteral("UTC%1%2")
        .arg(m_tzOffsetSec > 0 ? QStringLiteral("+") : QStringLiteral("-"))
        .arg(qAbs(m_tzOffsetSec) / 3600);
}

QString PassTableModel::jdMsToStr(int64_t jdMs) const
{
    // JD×86400000 毫秒 → 1970 epoch 毫秒（纯整数运算，无舍入/精度损失）
    qint64 msecs = jdMs - 210866760000000LL;  // 2440587.5 × 86400000
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC);
    if (m_tzOffsetSec != 0)
        dt = dt.toTimeZone(QTimeZone(m_tzOffsetSec));
    return dt.toString("HH:mm:ss");
}

int PassTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_passes.size();
}

int PassTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant PassTableModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const
{
    if (orientation != Qt::Horizontal || section < 0 || section >= ColCount)
        return QVariant();

    if (role == Qt::DisplayRole) {
        static const char *labels[ColCount] = {
            "#", "卫星名", "入境", "出境", "持续",
            "最大仰角", "方位角", "可见性", "太阳高度", "卫星光照",
            "可见弧段", "入境位置", "斜距", "多普勒", "星等", "质量"
        };
        // 时间列（入境/出境/可见弧段）内容均经 jdMsToStr 按时区偏移,
        // 表头须统一标注时区, 否则弧段列易被误读为 UTC
        if (section == ColAos || section == ColLos
            || section == ColArc)
            return QStringLiteral("%1(%2)").arg(labels[section]).arg(tzLabel());
        return QString::fromUtf8(labels[section]);
    }
    if (role == Qt::ToolTipRole) {
        static const char *tips[ColCount] = {
            nullptr, nullptr, nullptr, nullptr,
            "过境持续时长", "几何仰角（悬浮显示大气折射后的视仰角）",
            "TCA 时刻方位角", "光学可见 / 仅无线电 / 白天",
            "TCA 时刻太阳高度角", "TCA 时刻卫星日照状态（半影带为太阳圆面可见比例）",
            "几何过境内「暗天 + 被照亮」的连续可见时段",
            "入境时刻卫星星下点经纬度与高度",
            "TCA 时刻观测者至卫星斜距", "TCA 时刻多普勒频移",
            "视星等估算（Vallerie 相位模型），越小越亮",
            "综合质量评级"
        };
        return tips[section] ? QString::fromUtf8(tips[section]) : QVariant();
    }
    return QVariant();
}

QVariant PassTableModel::data(const QModelIndex &index, int role) const
{
    QMutexLocker locker(&m_dataMutex);
    
    if (!index.isValid() || index.row() < 0 || index.row() >= m_passes.size())
        return QVariant();
    const PassEvent &p = *m_passes[index.row()];
    const int c = index.column();
    // 列号防御: 与行号同级校验（视图协议保证只请求合法列, 防御越界数组访问）
    if (c < 0 || c >= ColCount)
        return QVariant();

    // 列对齐: 数值右对齐、状态居中、文本左对齐
    if (role == Qt::TextAlignmentRole) {
        static const Qt::Alignment aligns[ColCount] = {
            Qt::AlignCenter,                   /* # */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 卫星名 */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 入境 */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 出境 */
            Qt::AlignCenter,                   /* 持续 */
            Qt::AlignRight | Qt::AlignVCenter, /* 最大仰角 */
            Qt::AlignRight | Qt::AlignVCenter, /* 方位角 */
            Qt::AlignCenter,                   /* 可见性 */
            Qt::AlignRight | Qt::AlignVCenter, /* 太阳高度 */
            Qt::AlignCenter,                   /* 卫星光照 */
            Qt::AlignCenter,                   /* 可见弧段 */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 入境位置 */
            Qt::AlignRight | Qt::AlignVCenter, /* 斜距 */
            Qt::AlignRight | Qt::AlignVCenter, /* 多普勒 */
            Qt::AlignRight | Qt::AlignVCenter, /* 星等 */
            Qt::AlignCenter                    /* 质量 */
        };
        return QVariant::fromValue(aligns[c]);
    }

    if (role == Qt::ToolTipRole) {
        switch (c) {
        case ColDur:
            // 截断过境: 过境延续至预测窗口外, 出境时刻并非真实值
            if (p.truncated)
                return QStringLiteral(
                    "过境延续至预测窗口结束时刻（真实出境在窗口外, 持续时长被截断）");
            return QVariant();
        case ColMaxEl:
            // 大气折射后的视仰角（几何仰角带过 tooltip 展示, 仅显示用）
            if (p.tcaElRefracted > 0.0)
                return QStringLiteral("折射后最大仰角: %1°")
                    .arg(p.tcaElRefracted, 0, 'f', 1);
            return QVariant();
        case ColArc:
            if (p.hasVisibleArc)
                return QStringLiteral(
                    "几何过境内「暗天 + 被照亮」的连续时段，观测者实际可见范围");
            return QVariant();
        case ColMag:
            if (p.magValid)
                return QStringLiteral(
                    "视星等估算（Vallerie 相位模型），越小越亮\n"
                    "固有星等来自内置目录或输入框回退值，误差 ±1~2 星等");
            return QVariant();
        default:
            return QVariant();
        }
    }

    if (role == Qt::ForegroundRole) {
        switch (c) {
        case ColVis: {
            QColor col;
            switch (p.visibility) {
            case PassVisibility::Visible:   col = QColor(0, 150, 0); break;
            case PassVisibility::RadioOnly: col = QColor(0, 100, 180); break;
            case PassVisibility::Daylight:  col = QColor(150, 150, 150); break;
            }
            return QBrush(col);
        }
        case ColMaxEl: {
            QColor col;
            if (p.maxElevDeg >= 50.0)      col = QColor(0, 160, 0);
            else if (p.maxElevDeg >= 20.0) col = QColor(200, 150, 0);
            else                           col = QColor(200, 80, 0);
            return QBrush(col);
        }
        default:
            return QVariant();   // 默认前景色
        }
    }

    if (role != Qt::DisplayRole) return QVariant();

    // 释放锁再返回结果，避免长时间持有锁
    locker.unlock();
    
    switch (c) {
    case ColIndex:  return QString::number(index.row() + 1);
    case ColName:   return QString::fromStdString(p.satelliteName);
    case ColAos:    return jdMsToStr(p.aosJdMs);
    case ColLos:    return jdMsToStr(p.losJdMs);
    case ColDur:    return QStringLiteral("%1s%2")
                            .arg(static_cast<int>(p.durationSec))
                            .arg(p.truncated ? " *" : "");
    case ColMaxEl:  return QString::number(p.maxElevDeg, 'f', 1) + "°";
    case ColAz:     return QString::number(p.tcaLook.azDeg, 'f', 1) + "°";
    case ColVis: {
        switch (p.visibility) {
        case PassVisibility::Visible:   return QStringLiteral("可见");
        case PassVisibility::RadioOnly: return QStringLiteral("无线电");
        case PassVisibility::Daylight:  return QStringLiteral("白天");
        }
        return QVariant();
    }
    case ColSunAlt: return QString::number(p.tcaSunAlt, 'f', 1) + "°";
    case ColIllum: {
        switch (p.tcaSatShadow) {
        case ShadowStatus::FullSunlight: return QStringLiteral("日照");
        case ShadowStatus::InPenumbra:
            // 边界值 tcaIllum==1.0（理论刚出半影）显示 100% 而非钳制的 99%,
            // 与"日照"分类在界线上衔接一致
            return QStringLiteral("半影 %1%")
                .arg(qBound(1, static_cast<int>(p.tcaIllum * 100.0), 100));
        case ShadowStatus::InUmbra:      return QStringLiteral("本影");
        }
        return QVariant();
    }
    case ColArc:
        if (p.hasVisibleArc)
            return jdMsToStr(p.visAosJdMs) + " – " + jdMsToStr(p.visLosJdMs);
        return QStringLiteral("—");
    case ColAosGeo:
        return QStringLiteral("%1°, %2°, %3km")
            .arg(p.aosSatGeo.latDeg, 0, 'f', 1)
            .arg(p.aosSatGeo.lonDeg, 0, 'f', 1)
            .arg(p.aosSatGeo.altKm, 0, 'f', 0);
    case ColRange:  return QString::number(p.tcaLook.rangeKm, 'f', 0) + " km";
    case ColDoppler: return QString::number(p.dopplerAtTcaKHz, 'f', 2) + " kHz";
    case ColMag:
        return p.magValid ? QString::number(p.magAtTca, 'f', 1) : QStringLiteral("—");
    case ColQuality:
        switch (p.quality) {
        case PassQuality::Excellent: return QStringLiteral("★★★");
        case PassQuality::Good:      return QStringLiteral("★★");
        case PassQuality::Poor:      return QStringLiteral("★");
        }
        return QVariant();
    default:
        return QVariant();
    }
}