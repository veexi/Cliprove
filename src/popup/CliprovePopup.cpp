/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <Plasma/Plasma>
#include <Plasma/plasma_version.h>
#include <PlasmaQuick/PlasmaWindow>
#include <PlasmaQuick/SharedQmlEngine>

#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/plasmashell.h>
#include <KWayland/Client/plasmawindowmanagement.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/surface.h>
#include <KGlobalAccel>

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusReply>
#include <QGuiApplication>
#include <QEventLoopLocker>
#include <QQmlEngine>
#include <QQmlContext>
#include <QPointer>
#include <QQuickItem>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QThread>

#include <algorithm>
#include <memory>
#include "PasteController.h"
#include "../platform/WaylandDataControlBackend.h"

using namespace Qt::StringLiterals;

class CliprovePopup final : public PlasmaQuick::PlasmaWindow
{
    Q_OBJECT
public:
    CliprovePopup()
        : PlasmaQuick::PlasmaWindow()
    {
        engine_.engine()->rootContext()->setContextProperty(QStringLiteral("pasteController"), &pasteController_);
        clipboardWriter_ = new WaylandDataControlBackend;
        clipboardWriter_->setCaptureEnabled(false);
        clipboardWriter_->moveToThread(&clipboardThread_);
        connect(&clipboardThread_, &QThread::finished, clipboardWriter_, &QObject::deleteLater);
        clipboardThread_.start();
        QString clipboardError;
        bool clipboardReady = false;
        QMetaObject::invokeMethod(clipboardWriter_, [this, &clipboardReady, &clipboardError] {
            clipboardReady = clipboardWriter_->start(&clipboardError);
        }, Qt::BlockingQueuedConnection);
        if (clipboardReady) {
            pasteController_.publishPayload = [this](const MimePayloads &payloads, QString *error) {
                bool result = false;
                QMetaObject::invokeMethod(clipboardWriter_, [this, &payloads, error, &result] {
                    result = clipboardWriter_->setClipboard(payloads, error);
                }, Qt::BlockingQueuedConnection);
                return result;
            };
            connect(clipboardWriter_, &WaylandDataControlBackend::clipboardRead,
                    &pasteController_, &PasteController::clipboardRead);
        } else {
            qWarning().noquote() << "Image batch clipboard backend:" << clipboardError;
        }
        pasteController_.targetIsActive = [this] {
            return windowManagement_ ? (targetWindow_ && targetWindow_->isActive()) : !isActive();
        };
        connect(&pasteController_, &PasteController::hideRequested, this, [this] {
            savePosition();
            QWindow::hide();
            if (targetWindow_) targetWindow_->requestActivate();
        });
        connect(&pasteController_, &PasteController::failed, this, [this] {
            if (!isVisible()) showPopup();
        });
#if PLASMA_VERSION_MAJOR > 6 || (PLASMA_VERSION_MAJOR == 6 && PLASMA_VERSION_MINOR >= 7)
        Plasma::setupPlasmaStyle(engine_.engine().get());
#endif
        connect(engine_.engine().get(), &QQmlEngine::warnings, this, [](const QList<QQmlError> &warnings) {
            for (const auto &warning : warnings)
                qWarning().noquote() << warning.toString();
        });
        connect(&engine_, &PlasmaQuick::SharedQmlEngine::statusChanged, this, [](QQmlComponent::Status status) {
            qWarning() << "Cliprove popup QML status" << status;
        });
        engine_.setInitializationDelayed(true);
        connect(&engine_, &PlasmaQuick::SharedQmlEngine::finished,
                this, &CliprovePopup::onObjectIncubated);
#if PLASMA_VERSION_MAJOR > 6 || (PLASMA_VERSION_MAJOR == 6 && PLASMA_VERSION_MINOR >= 7)
        engine_.setSource(QUrl(QStringLiteral("qrc:/cliprove/popup/CliprovePopup.qml")));
#else
        engine_.setSource(QUrl(QStringLiteral("qrc:/cliprove/popup/legacy/CliprovePopup.qml")));
#endif
        engine_.completeInitialization();

        setTitle(QStringLiteral("Cliprove"));
        setFlags(Qt::Tool | Qt::FramelessWindowHint);

        saveTimer_.setSingleShot(true);
        saveTimer_.setInterval(300);
        connect(&saveTimer_, &QTimer::timeout, this, &CliprovePopup::savePosition);
        connect(this, &QWindow::xChanged, this, &CliprovePopup::scheduleSavePosition);
        connect(this, &QWindow::yChanged, this, &CliprovePopup::scheduleSavePosition);

        setupPlasmaShell();
    }

    ~CliprovePopup() override {
        pasteController_.cancel();
        clipboardThread_.quit();
        clipboardThread_.wait();
    }

    void toggle()
    {
        if (isVisible())
            hidePopup();
        else
            showPopup();
    }
    QString targetApplication() const { return targetWindow_ ? targetWindow_->appId() : QString(); }
    bool canTrackWindows() const { return bool(windowManagement_); }
public slots:
    void showPopup()
    {
        pasteController_.cancel();
        if (!isVisible() && windowManagement_) {
            auto *active = windowManagement_->activeWindow();
            targetWindow_ = active && active->appId() != QGuiApplication::desktopFileName()
                ? active : nullptr;
        }
        if (mainItem()) {
            const QString appId = targetWindow_ ? targetWindow_->appId() : QString();
            mainItem()->setProperty("terminalPaste", PasteController::usesShift(appId));
            qInfo().noquote() << "[paste-target]" << appId
                             << (PasteController::usesShift(appId) ? "Ctrl+Shift+V" : "Ctrl+V");
        }
        prepareGeometry();
        ensurePlasmaSurface();
        applyRememberedPosition();
        setVisible(true);
        requestActivate();
        QTimer::singleShot(0, this, [this] {
            applyRememberedPosition();
            requestActivate();
        });
    }

    void hidePopup()
    {
        pasteController_.cancel();
        savePosition();
        QWindow::hide();
    }

private:
    void setupPlasmaShell()
    {
        auto *registry = new KWayland::Client::Registry(this);
        auto *connection = KWayland::Client::ConnectionThread::fromApplication(qGuiApp);
        connect(registry, &KWayland::Client::Registry::plasmaWindowManagementAnnounced,
                this, [this, registry](quint32 name, quint32 version) {
            windowManagement_.reset(registry->createPlasmaWindowManagement(name, version));
        });
        connect(registry, &KWayland::Client::Registry::plasmaShellAnnounced,
                this, [this, registry](quint32 name, quint32 version) {
            if (!plasmaShell_) {
                plasmaShell_.reset(registry->createPlasmaShell(name, version));
                ensurePlasmaSurface();
                if (isVisible())
                    applyRememberedPosition();
            }
        });
        registry->create(connection);
        registry->setup();
    }

    void ensurePlasmaSurface()
    {
        if (!plasmaShell_ || plasmaSurface_)
            return;

        create();
        auto *surface = KWayland::Client::Surface::fromWindow(this);
        if (!surface)
            return;

        plasmaSurface_.reset(plasmaShell_->createSurface(surface, this));
        if (!plasmaSurface_)
            return;

        plasmaSurface_->setSkipTaskbar(true);
        plasmaSurface_->setSkipSwitcher(true);
        plasmaSurface_->setRole(KWayland::Client::PlasmaShellSurface::Role::Normal);
    }

    void onObjectIncubated()
    {
        auto *item = qobject_cast<QQuickItem *>(engine_.rootObject());
        if (!item)
            return;
        setMainItem(item);
        connect(this, &CliprovePopup::paddingChanged,
                this, &CliprovePopup::resizeToContent);
        connect(item, SIGNAL(requestHidePopup()), this, SLOT(hidePopup()));
    }
    void prepareGeometry()
    {
        if (!mainItem())
            return;

        QScreen *target = targetScreen();
        if (target)
            setScreen(target);

        QMetaObject::invokeMethod(mainItem(), "updateContentSize",
                                  Q_ARG(QSizeF, target ? target->availableSize().toSizeF()
                                                     : QSizeF(1280, 720)));
        resizeToContent();
    }

    void resizeToContent()
    {
        if (!mainItem())
            return;
        const QSize popupSize = QSize(mainItem()->implicitWidth(), mainItem()->implicitHeight())
            .grownBy(padding())
            .boundedTo(screen() ? screen()->availableSize() : QSize(1280, 720));
        resize(popupSize);
    }

    QScreen *activeScreen() const
    {
        const auto screens = QGuiApplication::screens();
        if (screens.size() <= 1)
            return QGuiApplication::primaryScreen();
        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                  QStringLiteral("/KWin"),
                                                  QStringLiteral("org.kde.KWin"),
                                                  QStringLiteral("activeOutputName"));
        const QDBusReply<QString> reply = QDBusConnection::sessionBus().call(msg);
        if (reply.isValid()) {
            const QString name = reply.value();
            const auto it = std::find_if(screens.cbegin(), screens.cend(),
                                         [&name](QScreen *s) { return s->name() == name; });
            if (it != screens.cend())
                return *it;
        }
        return QGuiApplication::primaryScreen();
    }

    QScreen *targetScreen() const
    {
        QSettings settings(QStringLiteral("veexi"), QStringLiteral("Cliprove"));
        if (settings.contains(QStringLiteral("popup/position"))) {
            const QPoint saved = settings.value(QStringLiteral("popup/position")).toPoint();
            for (QScreen *candidate : QGuiApplication::screens()) {
                if (candidate->availableGeometry().contains(saved))
                    return candidate;
            }
        }
        return activeScreen();
    }

    void applyRememberedPosition()
    {
        QScreen *target = targetScreen();
        if (target)
            setScreen(target);
        const QPoint pos = restoredPosition(target);
        if (plasmaSurface_)
            plasmaSurface_->setPosition(pos);
        QWindow::setPosition(pos);
    }

    QPoint restoredPosition(QScreen *target) const
    {
        if (!target)
            return QPoint(100, 100);

        QSettings settings(QStringLiteral("veexi"), QStringLiteral("Cliprove"));
        const QRect area = target->availableGeometry();
        QPoint pos;
        if (settings.contains(QStringLiteral("popup/position"))) {
            pos = settings.value(QStringLiteral("popup/position")).toPoint();
        } else {
            pos = QPoint(area.x() + (area.width() - width()) / 2,
                         area.y() + (area.height() - height()) / 2);
        }

        pos.setX(std::clamp(pos.x(), area.left(), std::max(area.left(), area.right() - width() + 1)));
        pos.setY(std::clamp(pos.y(), area.top(), std::max(area.top(), area.bottom() - height() + 1)));
        return pos;
    }

    void scheduleSavePosition()
    {
        if (isVisible())
            saveTimer_.start();
    }
    void savePosition()
    {
        if (!isVisible())
            return;
        QSettings settings(QStringLiteral("veexi"), QStringLiteral("Cliprove"));
        settings.setValue(QStringLiteral("popup/position"), position());
        settings.sync();
    }

    PasteController pasteController_;
    QThread clipboardThread_;
    WaylandDataControlBackend *clipboardWriter_ = nullptr;
    PlasmaQuick::SharedQmlEngine engine_;
    std::unique_ptr<KWayland::Client::PlasmaWindowManagement> windowManagement_;
    QPointer<KWayland::Client::PlasmaWindow> targetWindow_;
    std::unique_ptr<KWayland::Client::PlasmaShell> plasmaShell_;
    std::unique_ptr<KWayland::Client::PlasmaShellSurface> plasmaSurface_;
    QTimer saveTimer_;
};

class PopupService final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.veexi.CliprovePopup")

public:
    explicit PopupService(CliprovePopup *popup)
        : popup_(popup)
    {
    }

public slots:
    void toggle() { popup_->toggle(); }
    void show() { popup_->showPopup(); }
    void hide() { popup_->hidePopup(); }
    QString geometryInfo() const {
        const QRect g = popup_->geometry();
        return QStringLiteral("%1,%2 %3x%4 visible=%5")
            .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height())
            .arg(popup_->isVisible());
    }
    void moveTo(int x, int y) { popup_->setPosition(x, y); }
    QString targetApplication() const { return popup_->targetApplication(); }
    bool canTrackWindows() const { return popup_->canTrackWindows(); }

private:
    CliprovePopup *popup_;
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QEventLoopLocker keepAlive;

    QCoreApplication::setApplicationName(QStringLiteral("cliprove-popup"));
    QCoreApplication::setOrganizationName(QStringLiteral("veexi"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.veexi.cliprove-popup"));

    CliprovePopup popup;
    PopupService service(&popup);

    QAction showAction(&app);
    showAction.setObjectName(QStringLiteral("show-cliprove"));
    showAction.setText(QStringLiteral("Show Cliprove"));
    KGlobalAccel::setGlobalShortcut(&showAction, QKeySequence(QStringLiteral("Meta+V")));
    KGlobalAccel::self()->setShortcut(
        &showAction,
        {QKeySequence(QStringLiteral("Meta+V"))},
        KGlobalAccel::NoAutoloading);
    QObject::connect(&showAction, &QAction::triggered, &popup, &CliprovePopup::toggle);

    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.veexi.CliprovePopup")))
        return 2;
    if (!bus.registerObject(QStringLiteral("/Popup"), &service,
                            QDBusConnection::ExportAllSlots))
        return 3;

    return app.exec();
}

#include "CliprovePopup.moc"
