#include "core/HistoryStore.h"
#include "platform/ClipboardBackend.h"
#include "platform/PasteBackend.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QEventLoop>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

class FakeClipboard final : public ClipboardBackend {
public:
    bool start(QString *) override { return true; }
    QString backendName() const override { return QStringLiteral("fake clipboard"); }
    bool setClipboard(const MimePayloads &payloads, QString *) override {
        current = payloads.value(QStringLiteral("text/plain"));
        return !current.isEmpty();
    }
    QByteArray current;
};

class FakePaste final : public PasteBackend {
public:
    explicit FakePaste(FakeClipboard *clipboard) : clipboard_(clipboard) {}
    bool start(QString *) override { return true; }
    bool paste(QString *) override {
        pasted.append(clipboard_->current);
        return true;
    }
    bool isReady() const override { return true; }
    QString backendName() const override { return QStringLiteral("fake paste"); }
    QVector<QByteArray> pasted;
private:
    FakeClipboard *clipboard_;
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(QStringLiteral("veexi-test"));
    QCoreApplication::setApplicationName(QStringLiteral("Cliprove-ui-test"));
    QTemporaryDir dataDir;
    if (!dataDir.isValid()) return 1;
    qputenv("XDG_DATA_HOME", dataDir.path().toUtf8());

    HistoryStore store;
    QString error;
    if (!store.open(&error)) return 2;
    if (store.addEntry({{QStringLiteral("text/plain"), "first"}}, &error) <= 0) return 3;
    if (store.addEntry({{QStringLiteral("text/plain"), "second"}}, &error) <= 0) return 4;
    FakeClipboard clipboard;
    FakePaste paste(&clipboard);
    AppSettings settings;
    settings.batchIntervalMs = 200;
    settings.showThumbnails = false;
    MainWindow window(&store, &clipboard, &paste, settings);
    window.show();

    auto *list = window.findChild<QListWidget *>();
    if (!list || list->count() != 2) return 5;
    list->item(0)->setSelected(true);
    list->item(1)->setSelected(true);
    auto *pasteButton = window.findChild<QPushButton *>(QStringLiteral("pasteButton"));
    if (!pasteButton) return 6;
    pasteButton->click();

    QEventLoop loop;
    QTimer::singleShot(700, &loop, &QEventLoop::quit);
    loop.exec();
    if (paste.pasted != QVector<QByteArray>{"second", "first"}) return 7;

    window.showNormal();
    list->clearSelection();
    list->setCurrentItem(list->item(1));
    list->itemClicked(list->item(1));
    QTimer::singleShot(300, &loop, &QEventLoop::quit);
    loop.exec();
    if (paste.pasted.size() != 3 || paste.pasted.last() != "first") return 8;
    return 0;
}
