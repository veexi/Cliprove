#include "MainWindow.h"

#include "core/HistoryStore.h"
#include "platform/ClipboardBackend.h"
#include "platform/PasteBackend.h"

#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QShortcut>
#include <QSet>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

MainWindow::MainWindow(HistoryStore *store, ClipboardBackend *backend,
                       PasteBackend *pasteBackend, const AppSettings &settings,
                       QWidget *parent)
    : QMainWindow(parent),
      store_(store),
      backend_(backend),
      pasteBackend_(pasteBackend),
      settings_(settings) {
    setWindowTitle(QStringLiteral("Cliprove"));
    resize(760, 560);

    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);

    search_ = new QLineEdit(root);
    search_->setPlaceholderText(QStringLiteral("Search clipboard history..."));
    list_ = new QListWidget(root);
    list_->setAlternatingRowColors(true);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    status_ = new QLabel(QStringLiteral("Backend: %1").arg(backend_->backendName()), root);

    auto *star = new QPushButton(QStringLiteral("★ Star (Ctrl+D)"), root);
    auto *replay = new QPushButton(QStringLiteral("Paste selected (Enter)"), root);
    auto *settingsButton = new QPushButton(QStringLiteral("Settings"), root);
    auto *actions = new QHBoxLayout;
    actions->addWidget(star);
    actions->addWidget(replay);
    actions->addWidget(settingsButton);
    layout->addWidget(search_);
    layout->addWidget(list_, 1);
    layout->addLayout(actions);
    layout->addWidget(status_);
    setCentralWidget(root);

    connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(list_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this] { loadVisibleThumbnails(); });
    connect(replay, &QPushButton::clicked, this, &MainWindow::replayCurrent);
    connect(star, &QPushButton::clicked, this, &MainWindow::toggleStarred);
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);
    auto *starShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+D")), list_);
    starShortcut->setContext(Qt::WidgetShortcut);
    connect(starShortcut, &QShortcut::activated, this, &MainWindow::toggleStarred);
    auto *pasteShortcut = new QShortcut(QKeySequence(Qt::Key_Return), list_);
    pasteShortcut->setContext(Qt::WidgetShortcut);
    connect(pasteShortcut, &QShortcut::activated, this, &MainWindow::replayCurrent);
    connect(list_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *) {
        if (settings_.clickToPaste &&
            !(QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
            replayCurrent();
    });
    connect(list_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &position) {
        auto *item = list_->itemAt(position);
        if (!item) return;
        list_->setCurrentItem(item);
        QMenu menu(this);
        menu.addAction(item->data(Qt::UserRole + 1).toBool()
                           ? QStringLiteral("Unstar") : QStringLiteral("Star"),
                       this, &MainWindow::toggleStarred);
        menu.exec(list_->viewport()->mapToGlobal(position));
    });
    connect(backend_, &ClipboardBackend::backendError, this,
            [this](const QString &message) { status_->setText(message); });
    connect(pasteBackend_, &PasteBackend::statusChanged, this,
            [this](const QString &message) { status_->setText(message); });

    replayTimer_.setSingleShot(true);
    connect(&replayTimer_, &QTimer::timeout, this, [this] {
        if (replayStage_ == ReplayStage::Next) {
            replayNext();
            return;
        }
        if (replayStage_ != ReplayStage::Paste) return;
        QString error;
        if (!pasteBackend_->isReady() || !pasteBackend_->paste(&error)) {
            finishReplay(error.isEmpty() ? QStringLiteral("Direct paste is unavailable") : error);
            return;
        }
        ++replayIndex_;
        if (replayIndex_ == replayIds_.size()) {
            finishReplay();
        } else {
            replayStage_ = ReplayStage::Next;
            replayTimer_.start(settings_.batchIntervalMs);
        }
    });

    refresh();
}

void MainWindow::refresh() {
    const qint64 currentId = list_->currentItem()
        ? list_->currentItem()->data(Qt::UserRole).toLongLong() : 0;
    QSet<qint64> selectedIds;
    for (auto *item : list_->selectedItems())
        selectedIds.insert(item->data(Qt::UserRole).toLongLong());

    list_->clear();
    list_->setIconSize(QSize(settings_.thumbnailSize, settings_.thumbnailSize));
    QListWidgetItem *currentItem = nullptr;
    QString historyError;
    const auto entries = store_->recentEntries(1000, search_->text(), &historyError);
    for (const ClipboardEntry &entry : entries) {
        const QString time = entry.createdAt.toString(QStringLiteral("MM-dd HH:mm:ss"));
        auto *item = new QListWidgetItem(
            QStringLiteral("%1%2  %3")
                .arg(entry.starred ? QStringLiteral("★  ") : QString(), time, entry.summary), list_);
        item->setData(Qt::UserRole, entry.id);
        item->setData(Qt::UserRole + 1, entry.starred);
        item->setToolTip(QString(entry.formats).replace('\n', QStringLiteral(", ")));
        item->setData(Qt::UserRole + 2, settings_.showThumbnails &&
            (entry.formats.contains(QStringLiteral("image/")) ||
             entry.formats.contains(QStringLiteral("text/uri-list"))));
        if (selectedIds.contains(entry.id)) item->setSelected(true);
        if (entry.id == currentId) currentItem = item;
    }
    if (currentItem)
        list_->setCurrentItem(currentItem, QItemSelectionModel::NoUpdate);
    status_->setText(historyError.isEmpty()
                         ? QStringLiteral("%1 · %2 records")
                               .arg(backend_->backendName()).arg(list_->count())
                         : QStringLiteral("History read failed: %1").arg(historyError));
    QTimer::singleShot(0, this, &MainWindow::loadVisibleThumbnails);
}

void MainWindow::loadVisibleThumbnails() {
    if (loadingThumbnails_ || !isVisible() || !settings_.showThumbnails ||
        list_->count() == 0) return;
    loadingThumbnails_ = true;
    int first = list_->indexAt(QPoint(2, 2)).row();
    if (first < 0) first = 0;
    int last = list_->indexAt(QPoint(2, list_->viewport()->height() - 2)).row();
    if (last < first) last = std::min(list_->count() - 1, first + 20);
    for (int row = first; row <= last && row < list_->count(); ++row) {
        auto *item = list_->item(row);
        if (!item->data(Qt::UserRole + 2).toBool()) continue;
        item->setData(Qt::UserRole + 2, false);
        const QImage thumbnail = store_->thumbnailFor(item->data(Qt::UserRole).toLongLong());
        if (thumbnail.isNull()) continue;
        item->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
        item->setSizeHint(QSize(0, settings_.thumbnailSize + 10));
    }
    loadingThumbnails_ = false;
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    QTimer::singleShot(0, this, &MainWindow::loadVisibleThumbnails);
}

void MainWindow::toggleStarred() {
    auto *item = list_->currentItem();
    if (!item) return;
    const bool starred = !item->data(Qt::UserRole + 1).toBool();
    QString error;
    const auto selected = list_->selectedItems();
    for (auto *selectedItem : selected.isEmpty() ? QList<QListWidgetItem *>{item} : selected) {
        if (!store_->setStarred(selectedItem->data(Qt::UserRole).toLongLong(), starred, &error)) {
            status_->setText(QStringLiteral("Could not update star: %1").arg(error));
            refresh();
            return;
        }
    }
    refresh();
}

void MainWindow::replayCurrent() {
    if (!replayIds_.isEmpty()) return;
    auto selected = list_->selectedItems();
    if (selected.isEmpty() && list_->currentItem()) selected.append(list_->currentItem());
    if (selected.isEmpty()) return;
    std::sort(selected.begin(), selected.end(),
              [this](const QListWidgetItem *a, const QListWidgetItem *b) {
        return list_->row(a) < list_->row(b);
    });
    if (!pasteBackend_->isReady()) {
        if (selected.size() != 1) {
            status_->setText(QStringLiteral("Direct paste permission is required for multiple items."));
            return;
        }
        const MimePayloads payloads = store_->payloadsFor(
            selected.first()->data(Qt::UserRole).toLongLong());
        QString error;
        if (!backend_->setClipboard(payloads, &error))
            status_->setText(QStringLiteral("Replay failed: %1").arg(error));
        else
            status_->setText(QStringLiteral("Clipboard restored; direct paste is unavailable."));
        return;
    }

    replayIds_.reserve(selected.size());
    for (auto *item : selected)
        replayIds_.append(item->data(Qt::UserRole).toLongLong());
    replayIndex_ = 0;
    replayNext();
}

void MainWindow::replayNext() {
    if (replayIndex_ >= replayIds_.size()) return;
    const MimePayloads payloads = store_->payloadsFor(replayIds_[replayIndex_]);
    QString error;
    if (payloads.isEmpty() || !backend_->setClipboard(payloads, &error)) {
        finishReplay(error.isEmpty() ? QStringLiteral("History item could not be restored") : error);
        return;
    }
    if (replayIds_.isEmpty()) return;
    if (replayIndex_ == 0) {
        if (QSystemTrayIcon::isSystemTrayAvailable()) hide();
        else showMinimized();
    }
    replayStage_ = ReplayStage::Paste;
    replayTimer_.start(replayIndex_ == 0 ? 140 : 90);
}

void MainWindow::finishReplay(const QString &error) {
    const qsizetype completed = replayIndex_;
    const qsizetype total = replayIds_.size();
    replayTimer_.stop();
    replayIds_.clear();
    replayIndex_ = 0;
    replayStage_ = ReplayStage::Idle;
    if (error.isEmpty()) {
        status_->setText(QStringLiteral("Pasted %1 item(s)").arg(total));
    } else {
        qWarning() << "[replay] stopped after" << completed << "items:" << error;
        status_->setText(QStringLiteral("Pasted %1/%2 items: %3")
                             .arg(completed).arg(total).arg(error));
        show();
        raise();
        activateWindow();
    }
}

void MainWindow::onClipboardCaptured() {
    if (replayIds_.isEmpty()) return;
    finishReplay(QStringLiteral("Clipboard changed during batch paste"));
}

void MainWindow::showStoreError(const QString &error) {
    status_->setText(QStringLiteral("History save failed: %1").arg(error));
}

void MainWindow::openSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Cliprove settings"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;

    auto *batchInterval = new QSpinBox(&dialog);
    batchInterval->setRange(200, 5000);
    batchInterval->setSuffix(QStringLiteral(" ms"));
    batchInterval->setValue(settings_.batchIntervalMs);
    form->addRow(QStringLiteral("Delay between pasted items"), batchInterval);

    auto *historyLimit = new QSpinBox(&dialog);
    historyLimit->setRange(0, 100000);
    historyLimit->setSpecialValueText(QStringLiteral("Unlimited"));
    historyLimit->setValue(settings_.maxUnstarredEntries);
    form->addRow(QStringLiteral("Keep unstarred history"), historyLimit);

    auto *thumbnailSize = new QSpinBox(&dialog);
    thumbnailSize->setRange(40, 128);
    thumbnailSize->setSuffix(QStringLiteral(" px"));
    thumbnailSize->setValue(settings_.thumbnailSize);
    form->addRow(QStringLiteral("Thumbnail size"), thumbnailSize);

    auto *showThumbnails = new QCheckBox(QStringLiteral("Show image thumbnails"), &dialog);
    showThumbnails->setChecked(settings_.showThumbnails);
    form->addRow(showThumbnails);

    auto *clickToPaste = new QCheckBox(QStringLiteral("Single click pastes immediately"), &dialog);
    clickToPaste->setChecked(settings_.clickToPaste);
    form->addRow(clickToPaste);
    layout->addLayout(form);

    auto *hint = new QLabel(
        QStringLiteral("Starred items are never removed by the history limit. "
                       "Batch paste uses the visible list order."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    AppSettings next = settings_;
    next.batchIntervalMs = batchInterval->value();
    next.maxUnstarredEntries = historyLimit->value();
    next.thumbnailSize = thumbnailSize->value();
    next.showThumbnails = showThumbnails->isChecked();
    next.clickToPaste = clickToPaste->isChecked();
    QString error;
    if (!store_->setMaxUnstarredEntries(next.maxUnstarredEntries, &error)) {
        status_->setText(QStringLiteral("Settings failed: %1").arg(error));
        return;
    }
    if (!next.save()) {
        status_->setText(QStringLiteral("Settings could not be saved to disk."));
        return;
    }
    settings_ = next;
    refresh();
}
