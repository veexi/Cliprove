#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>
#include <QDir>
#include <QLockFile>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QStandardPaths>
#include <csignal>
#include <memory>

#include "core/HistoryStore.h"
#include "core/AppSettings.h"
#include "platform/ClipboardBackend.h"
#include "platform/PasteBackend.h"
#include "platform/PortalEisPasteBackend.h"
#include "platform/WaylandDataControlBackend.h"
#include "platform/X11ClipboardBackend.h"
#include "platform/X11PasteBackend.h"
#include "ui/MainWindow.h"

int main(int argc, char **argv) {
    std::signal(SIGPIPE, SIG_IGN);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(!QSystemTrayIcon::isSystemTrayAvailable());
    QApplication::setApplicationName(QStringLiteral("ClipTool"));
    QApplication::setApplicationDisplayName(QStringLiteral("Cliprove"));
    QApplication::setOrganizationName(QStringLiteral("veexi"));

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dataDir)) {
        QMessageBox::critical(nullptr, QStringLiteral("Cliprove"),
                              QStringLiteral("Could not create application data directory."));
        return 2;
    }
    QLockFile instanceLock(dataDir + QStringLiteral("/cliprove.lock"));
    if (!instanceLock.tryLock(0)) {
        QMessageBox::warning(nullptr, QStringLiteral("Cliprove"),
                             QStringLiteral("Cliprove is already running, or its lock is unavailable."));
        return 2;
    }

    HistoryStore store;
    QString error;
    if (!store.open(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("ClipTool"),
                              QStringLiteral("History database failed: %1").arg(error));
        return 2;
    }
    const AppSettings settings = AppSettings::load();
    if (!store.setMaxUnstarredEntries(settings.maxUnstarredEntries, &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("Cliprove"),
                              QStringLiteral("History cleanup failed: %1").arg(error));
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

    MainWindow window(&store, backend.get(), pasteBackend.get(), settings);
    QObject::connect(backend.get(), &ClipboardBackend::clipboardCaptured,
                     &window, [&](const MimePayloads &payloads) {
        if (payloads.size() == 1 &&
            payloads.contains(QStringLiteral("application/x-kde-onlyReplaceEmpty")))
            return;
        QString dbError;
        const qint64 id = store.addEntry(payloads, &dbError);
        window.refresh();
        window.onClipboardCaptured();
        if (id < 0) {
            qWarning() << "History insert failed:" << dbError;
            window.showStoreError(dbError);
        }
    });

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

    QIcon trayIconImage = QIcon::fromTheme(QStringLiteral("edit-paste"));
    if (trayIconImage.isNull())
        trayIconImage = app.style()->standardIcon(QStyle::SP_FileIcon);
    QMenu trayMenu;
    QSystemTrayIcon trayIcon(trayIconImage, &app);
    const auto showWindow = [&window] {
        window.show();
        window.raise();
        window.activateWindow();
    };
    auto *openAction = trayMenu.addAction(QStringLiteral("Open Cliprove"));
    QObject::connect(openAction, &QAction::triggered, &window, showWindow);
    trayMenu.addSeparator();
    auto *quitAction = trayMenu.addAction(QStringLiteral("Quit"));
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
    trayIcon.setContextMenu(&trayMenu);
    trayIcon.setToolTip(QStringLiteral("Cliprove"));
    QObject::connect(&trayIcon, &QSystemTrayIcon::activated, &window,
                     [showWindow](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger ||
            reason == QSystemTrayIcon::DoubleClick)
            showWindow();
    });
    trayIcon.show();
    window.show();
    return app.exec();
}
