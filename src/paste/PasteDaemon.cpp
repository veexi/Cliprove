/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "platform/PortalEisPasteBackend.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>

class PasteService final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.veexi.CliprovePaste")

public:
    explicit PasteService(PortalEisPasteBackend *backend, QObject *parent = nullptr)
        : QObject(parent), backend_(backend) {
        connect(backend_, &PasteBackend::statusChanged, this,
                [this](const QString &message) {
                    status_ = message;
                    qInfo().noquote() << "[paste-daemon]" << message;
                });
    }

public slots:
    bool paste() {
        QString error;
        const bool ok = backend_->paste(&error);
        if (!ok) {
            status_ = error.isEmpty() ? QStringLiteral("Paste failed") : error;
            qWarning().noquote() << "[paste-daemon]" << status_;
        }
        return ok;
    }

    bool ready() const {
        return backend_->isReady();
    }

    QString status() const {
        return status_;
    }

private:
    PortalEisPasteBackend *backend_;
    QString status_;
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cliprove-paste-daemon"));
    QCoreApplication::setOrganizationName(QStringLiteral("veexi"));

    PortalEisPasteBackend backend;
    PasteService service(&backend);

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCritical() << "Session D-Bus is unavailable";
        return 2;
    }
    if (!bus.registerService(QStringLiteral("org.veexi.CliprovePaste"))) {
        qCritical().noquote() << "Could not register D-Bus service:"
                              << bus.lastError().message();
        return 3;
    }
    if (!bus.registerObject(QStringLiteral("/Paste"), &service,
                            QDBusConnection::ExportAllSlots)) {
        qCritical().noquote() << "Could not register D-Bus object:"
                              << bus.lastError().message();
        return 4;
    }

    QString error;
    if (!backend.start(&error)) {
        qCritical().noquote() << "Could not start paste backend:" << error;
        return 5;
    }

    return app.exec();
}

#include "PasteDaemon.moc"
