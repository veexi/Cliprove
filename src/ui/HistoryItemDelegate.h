#pragma once

#include <QStyledItemDelegate>

class HistoryItemDelegate final : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit HistoryItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
};