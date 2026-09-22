#include "HistoryItemDelegate.h"

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace {
constexpr int kRoleStarred = Qt::UserRole + 1;
constexpr int kRoleType = Qt::UserRole + 3;
constexpr int kRoleTime = Qt::UserRole + 4;
constexpr int kRoleCompact = Qt::UserRole + 5;

QColor mint(const QPalette &pal, bool selected) {
    if (selected)
        return QColor(16, 185, 129, 30);
    return pal.base().color();
}

QColor borderColor(bool selected) {
    return selected ? QColor(QStringLiteral("#6ee7b7"))
                    : QColor(0, 0, 0, 0);
}

QString badgeText(const QString &type) {
    if (type == QStringLiteral("image")) return QStringLiteral("图");
    if (type == QStringLiteral("file")) return QStringLiteral("件");
    if (type == QStringLiteral("html")) return QStringLiteral("H");
    if (type == QStringLiteral("link")) return QStringLiteral("链");
    return QStringLiteral("文");
}

QColor badgeColor(const QString &type) {
    if (type == QStringLiteral("image")) return QColor(QStringLiteral("#dbeafe"));
    if (type == QStringLiteral("file")) return QColor(QStringLiteral("#fee2e2"));
    if (type == QStringLiteral("html")) return QColor(QStringLiteral("#ede9fe"));
    if (type == QStringLiteral("link")) return QColor(QStringLiteral("#cffafe"));
    return QColor(QStringLiteral("#d1fae5"));
}
}

HistoryItemDelegate::HistoryItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {}

QSize HistoryItemDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const {
    const bool hasIcon = !qvariant_cast<QIcon>(index.data(Qt::DecorationRole)).isNull();
    const bool compact = index.data(kRoleCompact).toBool();
    return QSize(0, compact ? (hasIcon ? 58 : 50) : (hasIcon ? 70 : 58));
}

void HistoryItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
    QRectF card = option.rect.adjusted(4, 3, -4, -3);

    QColor bg = mint(option.palette, selected);
    if (!selected && hovered)
        bg = option.palette.alternateBase().color();

    QPainterPath path;
    path.addRoundedRect(card, 10, 10);
    p->fillPath(path, bg);
    p->setPen(QPen(borderColor(selected), 1));
    p->drawPath(path);

    const int left = int(card.left()) + 12;
    const int centerY = int(card.center().y());
    const int iconSize = 42;
    QRect iconRect(left, centerY - iconSize / 2, iconSize, iconSize);

    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    const QString type = index.data(kRoleType).toString();
    if (!icon.isNull()) {
        icon.paint(p, iconRect, Qt::AlignCenter, QIcon::Normal);
    } else {
        p->setPen(Qt::NoPen);
        p->setBrush(badgeColor(type));
        p->drawRoundedRect(iconRect, 9, 9);
        QFont badgeFont = option.font;
        badgeFont.setBold(true);
        badgeFont.setPointSizeF(badgeFont.pointSizeF() + 1);
        p->setFont(badgeFont);
        p->setPen(QColor(QStringLiteral("#0f766e")));
        p->drawText(iconRect, Qt::AlignCenter, badgeText(type));
    }

    const int textLeft = iconRect.right() + 14;
    const int rightPad = 76;
    QRect titleRect(textLeft, int(card.top()) + 10,
                    int(card.right()) - textLeft - rightPad, 24);
    QRect metaRect(textLeft, titleRect.bottom() + 3,
                   titleRect.width(), 20);

    QFont titleFont = option.font;
    titleFont.setBold(true);
    p->setFont(titleFont);
    p->setPen(option.palette.text().color());
    const QString summary = option.fontMetrics.elidedText(
        index.data(Qt::DisplayRole).toString(), Qt::ElideRight, titleRect.width());
    p->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft, summary);

    QFont metaFont = option.font;
    metaFont.setPointSizeF(qMax(8.0, metaFont.pointSizeF() - 1.0));
    metaFont.setBold(false);
    p->setFont(metaFont);
    p->setPen(option.palette.mid().color());

    QString typeName = QStringLiteral("文本");
    if (type == QStringLiteral("image")) typeName = QStringLiteral("图片");
    else if (type == QStringLiteral("file")) typeName = QStringLiteral("文件");
    else if (type == QStringLiteral("html")) typeName = QStringLiteral("富文本");
    else if (type == QStringLiteral("link")) typeName = QStringLiteral("链接");
    p->drawText(metaRect, Qt::AlignVCenter | Qt::AlignLeft, typeName);

    QRect timeRect(int(card.right()) - 68, int(card.top()) + 11, 56, 20);
    p->drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter,
                index.data(kRoleTime).toString());

    if (index.data(kRoleStarred).toBool()) {
        QFont starFont = option.font;
        starFont.setPointSizeF(option.font.pointSizeF() + 3);
        p->setFont(starFont);
        p->setPen(QColor(QStringLiteral("#f59e0b")));
        QRect starRect(int(card.right()) - 32, int(card.bottom()) - 29, 20, 20);
        p->drawText(starRect, Qt::AlignCenter, QStringLiteral("★"));
    }

    p->restore();
}