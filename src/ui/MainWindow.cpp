#include "MainWindow.h"

#include "core/HistoryStore.h"
#include "platform/ClipboardBackend.h"
#include "platform/PasteBackend.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(HistoryStore *store, ClipboardBackend *backend,
                       PasteBackend *pasteBackend, QWidget *parent)
    : QMainWindow(parent),
      store_(store),
      backend_(backend),
      pasteBackend_(pasteBackend) {
    setWindowTitle(QStringLiteral("ClipTool PoC"));
    resize(760, 560);

    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);

    search_ = new QLineEdit(root);
    search_->setPlaceholderText(QStringLiteral("Search clipboard history..."));
    list_ = new QListWidget(root);
    list_->setAlternatingRowColors(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    status_ = new QLabel(QStringLiteral("Backend: %1").arg(backend_->backendName()), root);

    auto *replay = new QPushButton(QStringLiteral("Replay selected clipboard item"), root);
    layout->addWidget(search_);
    layout->addWidget(list_, 1);
    layout->addWidget(replay);
    layout->addWidget(status_);
    setCentralWidget(root);

    connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(replay, &QPushButton::clicked, this, &MainWindow::replayCurrent);
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { replayCurrent(); });
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
            QStringLiteral("%1  %2").arg(time, entry.summary), list_);
        item->setData(Qt::UserRole, entry.id);
        item->setToolTip(QString(entry.formats).replace('\n', QStringLiteral(", ")));
        if (entry.id == selectedId)
            list_->setCurrentItem(item);
    }
    status_->setText(QStringLiteral("%1 · %2 records")
                     .arg(backend_->backendName())
                     .arg(list_->count()));
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

    hide();
    QTimer::singleShot(140, this, [this] {
        QString pasteError;
        if (!pasteBackend_->paste(&pasteError))
            status_->setText(QStringLiteral("Direct paste failed: %1").arg(pasteError));
    });
}