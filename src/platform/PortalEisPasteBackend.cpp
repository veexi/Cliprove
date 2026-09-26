#include "PortalEisPasteBackend.h"

#include <QDebug>
#include <QSocketNotifier>

#include <libei.h>
#include <liboeffis.h>
#include <linux/input-event-codes.h>
#include <algorithm>

PortalEisPasteBackend::PortalEisPasteBackend(QObject *parent)
    : PasteBackend(parent) {
    reconnectTimer_.setSingleShot(true);
    connect(&reconnectTimer_, &QTimer::timeout, this, [this] {
        resetPortal();
        QString error;
        if (!start(&error)) {
            emit statusChanged(error);
            scheduleReconnect();
        }
    });
}

PortalEisPasteBackend::~PortalEisPasteBackend() {
    resetPortal();
}

void PortalEisPasteBackend::resetPortal() {
    reconnectTimer_.stop();
    resetEi();
    if (portalNotifier_) {
        portalNotifier_->setEnabled(false);
        delete portalNotifier_;
        portalNotifier_ = nullptr;
    }
    if (portal_) {
        portal_ = oeffis_unref(portal_);
    }
    started_ = false;
}

void PortalEisPasteBackend::scheduleReconnect() {
    if (reconnectTimer_.isActive()) return;
    reconnectTimer_.start(reconnectDelay_);
    reconnectDelay_ = std::min(reconnectDelay_ * 2, 30000);
}

QString PortalEisPasteBackend::backendName() const {
    return QStringLiteral("XDG RemoteDesktop Portal + libei");
}

bool PortalEisPasteBackend::isReady() const {
    return ready_;
}

bool PortalEisPasteBackend::start(QString *error) {
    if (started_)
        return true;

    portal_ = oeffis_new(this);
    if (!portal_) {
        if (error) *error = QStringLiteral("oeffis_new failed");
        return false;
    }

    portalNotifier_ = new QSocketNotifier(
        oeffis_get_fd(portal_), QSocketNotifier::Read, this);
    connect(portalNotifier_, &QSocketNotifier::activated,
            this, &PortalEisPasteBackend::onOeffisReadable);

    started_ = true;
    emit statusChanged(QStringLiteral("Waiting for keyboard input permission..."));
    oeffis_create_session(portal_, OEFFIS_DEVICE_KEYBOARD);
    return true;
}

void PortalEisPasteBackend::onOeffisReadable() {
    if (!portal_) return;

    oeffis_dispatch(portal_);
    while (true) {
        const auto event = oeffis_get_event(portal_);
        if (event == OEFFIS_EVENT_NONE)
            break;

        if (event == OEFFIS_EVENT_CONNECTED_TO_EIS) {
            const int fd = oeffis_get_eis_fd(portal_);
            if (fd < 0) {
                emit statusChanged(QStringLiteral("Portal connected, but no EIS fd"));
                resetPortal();
                scheduleReconnect();
                return;
            }

            resetEi();
            ei_ = ei_new_sender(this);
            ei_configure_name(ei_, "Cliprove");
            const int rc = ei_setup_backend_fd(ei_, fd);
            if (rc < 0) {
                emit statusChanged(QStringLiteral("libei backend setup failed: %1").arg(rc));
                resetPortal();
                scheduleReconnect();
                return;
            }

            eiNotifier_ = new QSocketNotifier(
                ei_get_fd(ei_), QSocketNotifier::Read, this);
            connect(eiNotifier_, &QSocketNotifier::activated,
                    this, &PortalEisPasteBackend::onEiReadable);
            emit statusChanged(QStringLiteral("Portal authorized; discovering keyboard device..."));
            processEiEvents();
        } else if (event == OEFFIS_EVENT_CLOSED) {
            setReady(false);
            emit statusChanged(QStringLiteral("Input permission session closed"));
            // An intentional closure must not reopen permission dialogs.
            resetPortal();
            return;
        } else if (event == OEFFIS_EVENT_DISCONNECTED) {
            setReady(false);
            const char *message = oeffis_get_error_message(portal_);
            const QString reason = QString::fromUtf8(message ? message : "unknown error");
            emit statusChanged(QStringLiteral("Portal disconnected: %1")
                               .arg(reason));
            resetPortal();
            // A refused permission request needs a new explicit user action,
            // not a series of automatically reopened permission dialogs.
            if (!reason.contains(QStringLiteral("denied"), Qt::CaseInsensitive)
                && !reason.contains(QStringLiteral("cancel"), Qt::CaseInsensitive)
                && !reason.contains(QStringLiteral("rejected"), Qt::CaseInsensitive))
                scheduleReconnect();
            return;
        }
    }
}

void PortalEisPasteBackend::onEiReadable() {
    processEiEvents();
}

void PortalEisPasteBackend::processEiEvents() {
    if (!ei_) return;

    ei_dispatch(ei_);
    while (auto *event = ei_get_event(ei_)) {
        const auto type = ei_event_get_type(event);
        switch (type) {
        case EI_EVENT_CONNECT:
            emit statusChanged(QStringLiteral("libei connected"));
            break;
        case EI_EVENT_SEAT_ADDED: {
            auto *seat = ei_event_get_seat(event);
            if (seat && ei_seat_has_capability(seat, EI_DEVICE_CAP_KEYBOARD)) {
                ei_seat_bind_capabilities(
                    seat, EI_DEVICE_CAP_KEYBOARD, nullptr);
            }
            break;
        }
        case EI_EVENT_DEVICE_ADDED: {
            auto *device = ei_event_get_device(event);
            if (device && ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD)) {
                if (keyboard_)
                    keyboard_ = ei_device_unref(keyboard_);
                keyboard_ = ei_device_ref(device);
                keyboardResumed_ = false;
                emit statusChanged(QStringLiteral("Keyboard injection device found"));
            }
            break;
        }
        case EI_EVENT_DEVICE_RESUMED: {
            auto *device = ei_event_get_device(event);
            if (device && device == keyboard_) {
                keyboardResumed_ = true;
                reconnectDelay_ = 1000;
                reconnectTimer_.stop();
                setReady(true);
                emit statusChanged(QStringLiteral("Direct paste ready"));
            }
            break;
        }
        case EI_EVENT_DEVICE_PAUSED: {
            auto *device = ei_event_get_device(event);
            if (device && device == keyboard_) {
                keyboardResumed_ = false;
                setReady(false);
                emit statusChanged(QStringLiteral("Keyboard injection paused"));
            }
            break;
        }
        case EI_EVENT_DEVICE_REMOVED: {
            auto *device = ei_event_get_device(event);
            if (device && device == keyboard_) {
                keyboard_ = ei_device_unref(keyboard_);
                keyboardResumed_ = false;
                setReady(false);
                emit statusChanged(QStringLiteral("Keyboard injection device removed"));
            }
            break;
        }
        case EI_EVENT_DISCONNECT:
            setReady(false);
            keyboardResumed_ = false;
            if (eiNotifier_) eiNotifier_->setEnabled(false);
            emit statusChanged(QStringLiteral("libei disconnected"));
            // Defer teardown until after the event has been released. A CLOSED
            // portal event arriving meanwhile cancels this retry.
            scheduleReconnect();
            break;
        default:
            break;
        }
        ei_event_unref(event);
    }
}

bool PortalEisPasteBackend::paste(QString *error) {
    return pasteWithShift(false, error);
}

bool PortalEisPasteBackend::pasteWithShift(bool shift, QString *error) {
    if (!ready_ || !keyboard_ || !keyboardResumed_) {
        if (error)
            *error = QStringLiteral("Keyboard injection is not ready");
        return false;
    }
    return sendCtrlV(shift, error);
}

bool PortalEisPasteBackend::sendCtrlV(bool shift, QString *error) {
    if (!keyboard_) {
        if (error) *error = QStringLiteral("No keyboard device");
        return false;
    }

    ei_device_start_emulating(keyboard_, sequence_++);

    ei_device_keyboard_key(keyboard_, KEY_LEFTCTRL, true);
    ei_device_frame(keyboard_, ei_now(ei_));

    if (shift) {
        ei_device_keyboard_key(keyboard_, KEY_LEFTSHIFT, true);
        ei_device_frame(keyboard_, ei_now(ei_));
    }

    ei_device_keyboard_key(keyboard_, KEY_V, true);
    ei_device_frame(keyboard_, ei_now(ei_));

    ei_device_keyboard_key(keyboard_, KEY_V, false);
    ei_device_frame(keyboard_, ei_now(ei_));

    if (shift) {
        ei_device_keyboard_key(keyboard_, KEY_LEFTSHIFT, false);
        ei_device_frame(keyboard_, ei_now(ei_));
    }

    ei_device_keyboard_key(keyboard_, KEY_LEFTCTRL, false);
    ei_device_frame(keyboard_, ei_now(ei_));

    ei_device_stop_emulating(keyboard_);
    emit statusChanged(shift ? QStringLiteral("Ctrl+Shift+V injected") : QStringLiteral("Ctrl+V injected"));
    return true;
}

void PortalEisPasteBackend::setReady(bool ready) {
    if (ready_ == ready) return;
    ready_ = ready;
    emit readyChanged(ready_);
}

void PortalEisPasteBackend::resetEi() {
    setReady(false);
    keyboardResumed_ = false;

    if (eiNotifier_) {
        eiNotifier_->setEnabled(false);
        delete eiNotifier_;
        eiNotifier_ = nullptr;
    }
    if (keyboard_)
        keyboard_ = ei_device_unref(keyboard_);
    if (ei_)
        ei_ = ei_unref(ei_);
}
