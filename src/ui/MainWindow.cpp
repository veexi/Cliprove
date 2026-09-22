#include "MainWindow.h"

#include "core/HistoryStore.h"
#include "platform/ClipboardBackend.h"
#include "platform/PasteBackend.h"

#include <QDebug>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(HistoryStore *store, ClipboardBackend *backend,
                       PasteBackend *pasteBackend, QWidget *parent)
    : QMainWindow(parent),
      store_(store),
      backend_(backend),
      pasteBackend_(pasteBackend) {
    setWindowTitle(QStringLiteral("Cliprove"));
    resize(760, 560);

    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);

    search_ = new QLineEdit(root);
    search_->setPlaceholderText(QStringLiteral("Search clipboard history..."));
    list_ = new QListWidget(root);
    list_->setAlternatingRowColors(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    status_ = new QLabel(QStringLiteral("Backend: %1").arg(backend_->backendName()), root);

    auto *star = new QPushButton(QStringLiteral("Star / unstar selected item (Ctrl+D)"), root);
    auto *replay = new QPushButton(QStringLiteral("Paste selected item"), root);
    layout->addWidget(search_);
    layout->addWidget(list_, 1);
    layout->addWidget(star);
    layout->addWidget(replay);
    layout->addWidget(status_);
    setCentralWidget(root);

    connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(replay, &QPushButton::clicked, this, &MainWindow::replayCurrent);
    connect(star, &QPushButton::clicked, this, &MainWindow::toggleStarred);
    auto *starShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+D")), this);
    connect(starShortcut, &QShortcut::activated, this, &MainWindow::toggleStarred);
    connect(list_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *) { replayCurrent(); });
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

    refresh();
}

void MainWindow::refresh() {
    const qint64 selectedId = list_->currentItem()
        ? list_->currentItem()->data(Qt::UserRole).toLongLong() : 0;

    list_->clear();
    for (const ClipboardEntry &entry : store_->recentEntries(1000, search_->text())) {
        const QString time = entry.createdAt.toString(QStringLiteral("MM-dd HH:mm:ss"));
        auto *item = new QListWidgetItem(
            QStringLiteral("%1%2  %3")
                .arg(entry.starred ? QStringLiteral("★  ") : QString(), time, entry.summary), list_);
        item->setData(Qt::UserRole, entry.id);
        item->setData(Qt::UserRole + 1, entry.starred);
        item->setToolTip(QString(entry.formats).replace('\n', QStringLiteral(", ")));
        if (entry.id == selectedId)
            list_->setCurrentItem(item);
    }
    status_->setText(QStringLiteral("%1 · %2 records")
                     .arg(backend_->backendName())
                     .arg(list_->count()));
}

void MainWindow::toggleStarred() {
    auto *item = list_->currentItem();
    if (!item) return;
    QString error;
    if (!store_->setStarred(item->data(Qt::UserRole).toLongLong(),
                            !item->data(Qt::UserRole + 1).toBool(), &error)) {
        status_->setText(QStringLiteral("Could not update star: %1").arg(error));
        return;
    }
    refresh();
}

void MainWindow::replayCurrent() {
    auto *item = list_->currentItem();
    if (!item) return;

    const qint64 id = item->data(Qt::UserRole).toLongLong();
    const MimePayloads payloads = store_->payloadsFor(id);
    QString error;
    if (!backend_->setClipboard(payloads, &error)) {
        status_->setText(QStringLiteral("Replay failed: %1").arg(error));
        return;
    }
    if (!pasteBackend_->isReady()) {
        status_->setText(QStringLiteral("Clipboard restored; direct-paste permission is not ready yet."));
        return;
    }

    if (QSystemTrayIcon::isSystemTrayAvailable())
        hide();
    else
        showMinimized();
    QTimer::singleShot(140, this, [this] {
        QString pasteError;
        if (!pasteBackend_->paste(&pasteError)) {
            qWarning() << "[replay] direct paste failed:" << pasteError;
            status_->setText(QStringLiteral("Direct paste failed: %1").arg(pasteError));
            show();
            raise();
            activateWindow();
        }
    });
}
