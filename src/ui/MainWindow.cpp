#include "MainWindow.h"

#include "core/HistoryStore.h"
#include "platform/ClipboardBackend.h"
#include "platform/PasteBackend.h"
#include "ui/SettingsDialog.h"
#include "ui/HistoryItemDelegate.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QDebug>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QShortcut>
#include <QSet>
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
    setWindowIcon(Theme::windowIcon());
    resize(860, 620);
    setMinimumSize(720, 500);

    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(16, 16, 16, 14);
    layout->setSpacing(12);

    auto *topBar = new QWidget(root);
    topBar->setObjectName(QStringLiteral("topBar"));
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(14, 10, 12, 10);
    topLayout->setSpacing(10);

    auto *brandIcon = new QLabel(topBar);
    brandIcon->setPixmap(Theme::appIcon().pixmap(QSize(34, 34)));
    brandIcon->setFixedSize(36, 36);

    auto *brandColumn = new QVBoxLayout;
    brandColumn->setSpacing(0);
    auto *brandTitle = new QLabel(QStringLiteral("Cliprove"), topBar);
    brandTitle->setObjectName(QStringLiteral("brandTitle"));
    auto *brandSubtitle = new QLabel(QStringLiteral("剪贴板历史"), topBar);
    brandSubtitle->setObjectName(QStringLiteral("brandSubtitle"));
    brandColumn->addWidget(brandTitle);
    brandColumn->addWidget(brandSubtitle);

    search_ = new QLineEdit(topBar);
    search_->setClearButtonEnabled(true);
    search_->setPlaceholderText(QStringLiteral("搜索剪贴板历史…"));

    settingsButton_ = new QPushButton(QStringLiteral("⚙"), topBar);
    settingsButton_->setObjectName(QStringLiteral("iconButton"));
    settingsButton_->setToolTip(QStringLiteral("设置"));

    topLayout->addWidget(brandIcon);
    topLayout->addLayout(brandColumn);
    topLayout->addSpacing(10);
    topLayout->addWidget(search_, 1);
    topLayout->addWidget(settingsButton_);

    list_ = new QListWidget(root);
    list_->setObjectName(QStringLiteral("historyList"));
    list_->setItemDelegate(new HistoryItemDelegate(list_));
    list_->setAlternatingRowColors(false);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    starButton_ = new QPushButton(QStringLiteral("★ 收藏  Ctrl+D"), root);
    starButton_->setObjectName(QStringLiteral("starButton"));
    replayButton_ = new QPushButton(QStringLiteral("粘贴所选  Enter"), root);
    replayButton_->setObjectName(QStringLiteral("pasteButton"));
    replayButton_->setProperty("primary", true);
    actions->addWidget(starButton_);
    actions->addStretch();
    actions->addWidget(replayButton_);

    auto *statusBar = new QWidget(root);
    statusBar->setObjectName(QStringLiteral("statusBar"));
    auto *statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(12, 7, 12, 7);
    statusLayout->setSpacing(10);
    auto *monitorDot = new QLabel(QStringLiteral("●"), statusBar);
    monitorDot->setStyleSheet(QStringLiteral("color:#10b981;"));
    status_ = new QLabel(QStringLiteral("正在监听剪贴板"), statusBar);
    status_->setObjectName(QStringLiteral("statusDetail"));
    countLabel_ = new QLabel(statusBar);
    countLabel_->setObjectName(QStringLiteral("statusDetail"));
    auto *backendLabel = new QLabel(backend_->backendName(), statusBar);
    backendLabel->setObjectName(QStringLiteral("statusDetail"));
    statusLayout->addWidget(monitorDot);
    statusLayout->addWidget(status_);
    statusLayout->addSpacing(4);
    statusLayout->addWidget(countLabel_);
    statusLayout->addStretch();
    statusLayout->addWidget(backendLabel);

    layout->addWidget(topBar);
    layout->addWidget(list_, 1);
    layout->addLayout(actions);
    layout->addWidget(statusBar);
    setCentralWidget(root);
    applyVisualStyle();

    connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(list_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this] { loadVisibleThumbnails(); });
    connect(replayButton_, &QPushButton::clicked, this, &MainWindow::replayCurrent);
    connect(starButton_, &QPushButton::clicked, this, &MainWindow::toggleStarred);
    connect(settingsButton_, &QPushButton::clicked, this, &MainWindow::openSettings);
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
            [this](const QString &message) { showStatus(message, true); });
    connect(pasteBackend_, &PasteBackend::statusChanged, this,
            [this](const QString &message) { showStatus(message); });

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
        const QString time = entry.createdAt.toString(QStringLiteral("HH:mm"));
        auto *item = new QListWidgetItem(entry.summary, list_);
        item->setData(Qt::UserRole, entry.id);
        item->setData(Qt::UserRole + 1, entry.starred);
        item->setToolTip(QString(entry.formats).replace('\n', QStringLiteral(", ")));
        item->setData(Qt::UserRole + 2, settings_.showThumbnails &&
            (entry.formats.contains(QStringLiteral("image/")) ||
             entry.formats.contains(QStringLiteral("text/uri-list"))));

        QString type = QStringLiteral("text");
        if (entry.formats.contains(QStringLiteral("image/")))
            type = QStringLiteral("image");
        else if (entry.formats.contains(QStringLiteral("text/uri-list")))
            type = QStringLiteral("file");
        else if (entry.formats.contains(QStringLiteral("text/html")))
            type = QStringLiteral("html");
        else if (entry.formats.contains(QStringLiteral("text/x-moz-url")) ||
                 entry.summary.startsWith(QStringLiteral("http://")) ||
                 entry.summary.startsWith(QStringLiteral("https://")))
            type = QStringLiteral("link");

        item->setData(Qt::UserRole + 3, type);
        item->setData(Qt::UserRole + 4, time);
        item->setData(Qt::UserRole + 5, settings_.compactRows);
        if (selectedIds.contains(entry.id)) item->setSelected(true);
        if (entry.id == currentId) currentItem = item;
    }
    if (currentItem)
        list_->setCurrentItem(currentItem, QItemSelectionModel::NoUpdate);
    countLabel_->setText(QStringLiteral("共 %1 条记录").arg(list_->count()));
    if (!historyError.isEmpty())
        showStatus(QStringLiteral("读取历史失败：%1").arg(historyError), true);
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
    }
    loadingThumbnails_ = false;
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    if (!hasAnimatedShow_ && settings_.animationsEnabled) {
        hasAnimatedShow_ = true;
        animateWindowIn();
    }
    QTimer::singleShot(0, this, &MainWindow::loadVisibleThumbnails);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (settings_.closeToTray && QSystemTrayIcon::isSystemTrayAvailable()) {
        event->ignore();
        hide();
        showStatus(QStringLiteral("Cliprove 将继续在系统托盘运行"));
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::applyVisualStyle() {
    list_->setSpacing(settings_.compactRows ? 0 : 1);
    list_->setIconSize(QSize(settings_.thumbnailSize, settings_.thumbnailSize));
    search_->setMinimumWidth(260);
}

void MainWindow::animateWindowIn() {
    if (!centralWidget()) return;
    auto *effect = new QGraphicsOpacityEffect(centralWidget());
    centralWidget()->setGraphicsEffect(effect);
    effect->setOpacity(0.25);

    auto *animation = new QPropertyAnimation(effect, "opacity", centralWidget());
    animation->setDuration(160);
    animation->setStartValue(0.25);
    animation->setEndValue(1.0);
    connect(animation, &QPropertyAnimation::finished, centralWidget(), [this] {
        if (centralWidget())
            centralWidget()->setGraphicsEffect(nullptr);
    });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::showStatus(const QString &message, bool error) {
    status_->setText(message);
    status_->setStyleSheet(error
        ? QStringLiteral("color:#dc2626;")
        : QString());
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
    SettingsDialog dialog(settings_, store_, this);
    dialog.setWindowIcon(Theme::windowIcon());
    if (dialog.exec() != QDialog::Accepted)
        return;

    const AppSettings next = dialog.settings();
    QString error;
    if (!store_->setMaxUnstarredEntries(next.maxUnstarredEntries, &error)) {
        showStatus(QStringLiteral("设置失败：%1").arg(error), true);
        return;
    }
    if (!next.save()) {
        showStatus(QStringLiteral("设置无法保存到磁盘"), true);
        return;
    }

    settings_ = next;
    applyVisualStyle();
    refresh();
    showStatus(QStringLiteral("设置已保存"));
}
