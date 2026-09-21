#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QGuiApplication>
#include <memory>

#include "core/HistoryStore.h"
#include "platform/ClipboardBackend.h"
#include "platform/PasteBackend.h"
#include "platform/PortalEisPasteBackend.h"
#include "platform/WaylandDataControlBackend.h"
#include "platform/X11ClipboardBackend.h"
#include "platform/X11PasteBackend.h"
#include "ui/MainWindow.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ClipTool"));
    QApplication::setOrganizationName(QStringLiteral("veexi"));

    HistoryStore store;
    QString error;
    if (!store.open(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("ClipTool"),
                              QStringLiteral("History database failed: %1").arg(error));
        return 2;
    }

    const QString platform = QGuiApplication::platformName().toLower();

    std::unique_ptr<ClipboardBackend> backend;
    std::unique_ptr<PasteBackend> pasteBackend;

    if (platform.contains(QStringLiteral("xcb"))) {
        backend = std::make_unique<X11ClipboardBackend>();
        pasteBackend = std::make_unique<X11PasteBackend>();
    } else {
        backend = std::make_unique<WaylandDataControlBackend>();
        pasteBackend = std::make_unique<PortalEisPasteBackend>();
    }

    if (!backend->start(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("ClipTool"),
                              QStringLiteral("Clipboard backend failed: %1").arg(error));
        return 3;
    }

    QObject::connect(pasteBackend.get(), &PasteBackend::statusChanged,
                     [](const QString &message) {
        qInfo().noquote() << "[paste]" << message;
    });
    QObject::connect(pasteBackend.get(), &PasteBackend::readyChanged,
                     [](bool ready) {
        qInfo() << "[paste] ready =" << ready;
    });

    QString pasteError;
    if (!pasteBackend->start(&pasteError))
        qWarning() << "Direct paste backend failed to start:" << pasteError;

    MainWindow window(&store, backend.get(), pasteBackend.get());
    QObject::connect(backend.get(), &ClipboardBackend::clipboardCaptured,
                     &window, [&](const MimePayloads &payloads) {
        QString dbError;
        const qint64 id = store.addEntry(payloads, &dbError);
        if (id < 0)
            qWarning() << "History insert failed:" << dbError;
        window.refresh();
    });

    window.show();
    return app.exec();
}
