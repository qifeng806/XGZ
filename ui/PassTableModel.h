#ifndef UI_PASSTABLEMODEL_H
#define UI_PASSTABLEMODEL_H
// =============================================================================
// PassTableModel.h — 过境列表懒加载模型（QTableView + model 方案, M 修复）
// 数据为共享指针集（指向 MainWindow 持有的过境集合, 零深拷贝）;
// QTableView 仅请求可见单元格的 data(), 千颗卫星级批量不再整表逐格
// 创建 QTableWidgetItem（此前每行 16 个 item 全量构建是刷新卡顿根源）
// =============================================================================
#include <QAbstractTableModel>
#include <QVector>
#include <QMutex>
#include <QMutexLocker>
#include "core/models.h"

class PassTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColIndex = 0, ColName, ColAos, ColLos, ColDur, ColMaxEl,
        ColAz, ColVis, ColSunAlt, ColIllum, ColArc, ColAosGeo,
        ColRange, ColDoppler, ColMag, ColQuality,
        ColCount
    };

    explicit PassTableModel(QObject *parent = nullptr);

    // 共享数据指针集（指向调用方持有的 PassEvent, 模型不持有所有权）
    void setPasses(const QVector<const PassEvent *> &passes);
    const QVector<const PassEvent *> &passes() const { return m_passes; }

    // 时区（秒偏移）: 更新入境/出境表头与时间列显示
    void setTimeZoneOffset(int tzOffsetSec);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

private:
    QVector<const PassEvent *> m_passes;
    int m_tzOffsetSec = 0;   // 0 = UTC
    mutable QMutex m_dataMutex;  // 保护数据的互斥锁

    QString tzLabel() const;
    QString jdMsToStr(int64_t jdMs) const;
};

#endif // UI_PASSTABLEMODEL_H