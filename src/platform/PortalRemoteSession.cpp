#include "PortalRemoteSession.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDebug>
#include <QUuid>

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <unistd.h>

namespace {
constexpr auto kService = "org.freedesktop.portal.Desktop";
constexpr auto kDesktopPath = "/org/freedesktop/portal/desktop";
constexpr auto kRemoteDesktop = "org.freedesktop.portal.RemoteDesktop";
constexpr auto kClipboard = "org.freedesktop.portal.Clipboard";
constexpr auto kRequest = "org.freedesktop.portal.Request";
constexpr auto kSession = "org.freedesktop.portal.Session";
constexpr qsizetype kMaxMimeBytes = 64 * 1024 * 1024;
constexpr int kReadTimeoutMs = 2000;
constexpr uint kKeyboardDevice = 1;
constexpr int kKeysymControlL = 0xffe3;
constexpr int kKeysymV = 0x76;
}

PortalRemoteSession::PortalRemoteSession(QObject *parent)
    : QObject(parent) {}

PortalRemoteSession::~PortalRemoteSession() {
    clearRequestWatch();
    if (!sessionPath_.isEmpty()) {
        QDBusInterface session(kService, sessionPath_, kSession,
                               QDBusConnection::sessionBus());
        session.call(QDBus::NoBlock, QStringLiteral("Close"));
    }
}

QString PortalRemoteSession::newToken(const QString &prefix) const {
    QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    token.replace('-', '_');
    return prefix + '_' + token;
}

bool PortalRemoteSession::start(QString *error) {
    if (step_ != Step::Idle && step_ != Step::Failed)
        return true;

    if (!QDBusConnection::sessionBus().isConnected()) {
        if (error) *error = QStringLiteral("Session D-Bus is unavailable");
        return false;
    }

    step_ = Step::Creating;
    setStatus(QStringLiteral("Creating desktop portal session..."));
    return createSession(error);
}

bool PortalRemoteSession::isReady() const { return ready_; }
bool PortalRemoteSession::clipboardReady() const { return clipboardReady_; }
QString PortalRemoteSession::status() const { return status_; }

void PortalRemoteSession::setStatus(const QString &status) {
    status_ = status;
    emit statusChanged(status_);
}

void PortalRemoteSession::setReadyState(bool inputReady, bool clipboardReady) {
    if (ready_ != inputReady) {
        ready_ = inputReady;
        emit readyChanged(ready_);
    }
    if (clipboardReady_ != clipboardReady) {
        clipboardReady_ = clipboardReady;
        emit clipboardReadyChanged(clipboardReady_);
    }
}

void PortalRemoteSession::fail(const QString &message) {
    step_ = Step::Failed;
    setReadyState(false, false);
    setStatus(message);
}

bool PortalRemoteSession::createSession(QString *error) {
    QDBusInterface iface(kService, kDesktopPath, kRemoteDesktop,
                         QDBusConnection::sessionBus());

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"),
                   newToken(QStringLiteral("create")));
    options.insert(QStringLiteral("session_handle_token"),
                   newToken(QStringLiteral("session")));

    QDBusReply<QDBusObjectPath> reply =
        iface.call(QStringLiteral("CreateSession"), options);
    if (!reply.isValid()) {
        if (error) *error = reply.error().message();
        fail(QStringLiteral("CreateSession failed: %1")
             .arg(reply.error().message()));
        return false;
    }

    return watchRequest(reply.value().path(), Step::Creating, error);
}

bool PortalRemoteSession::watchRequest(
    const QString &requestPath, Step step, QString *error) {
    clearRequestWatch();
    requestPath_ = requestPath;
    step_ = step;

    const bool ok = QDBusConnection::sessionBus().connect(
        kService, requestPath_, kRequest, QStringLiteral("Response"),
        this, SLOT(onRequestResponse(uint,QVariantMap)));
    if (!ok) {
        if (error)
            *error = QStringLiteral("Could not watch portal request %1")
                         .arg(requestPath);
        fail(QStringLiteral("Portal request signal connection failed"));
        return false;
    }
    return true;
}

void PortalRemoteSession::clearRequestWatch() {
    if (requestPath_.isEmpty()) return;
    QDBusConnection::sessionBus().disconnect(
        kService, requestPath_, kRequest, QStringLiteral("Response"),
        this, SLOT(onRequestResponse(uint,QVariantMap)));
    requestPath_.clear();
}

void PortalRemoteSession::onRequestResponse(
    uint response, const QVariantMap &results) {
    const Step completedStep = step_;
    clearRequestWatch();

    if (response != 0) {
        fail(QStringLiteral("Portal request denied/cancelled (%1)").arg(response));
        return;
    }

    QString error;
    if (completedStep == Step::Creating) {
        sessionPath_ = results.value(QStringLiteral("session_handle")).toString();
        if (sessionPath_.isEmpty()) {
            fail(QStringLiteral("Portal returned no session handle"));
            return;
        }

        QDBusConnection::sessionBus().connect(
            kService, sessionPath_, kSession, QStringLiteral("Closed"),
            this, SLOT(onSessionClosed(QVariantMap)));

        QDBusConnection::sessionBus().connect(
            kService, kDesktopPath, kClipboard,
            QStringLiteral("SelectionOwnerChanged"),
            this, SLOT(onSelectionOwnerChanged(QDBusObjectPath,QVariantMap)));
        QDBusConnection::sessionBus().connect(
            kService, kDesktopPath, kClipboard,
            QStringLiteral("SelectionTransfer"),
            this, SLOT(onSelectionTransfer(QDBusObjectPath,QString,uint)));

        if (!requestClipboard(&error) || !selectDevices(&error)) {
            fail(error);
            return;
        }
        setStatus(QStringLiteral("Waiting for desktop-control permission..."));
        return;
    }

    if (completedStep == Step::Selecting) {
        if (!startSession(&error)) {
            fail(error);
            return;
        }
        setStatus(QStringLiteral("Waiting for portal authorization..."));
        return;
    }

    if (completedStep == Step::Starting) {
        const uint devices = results.value(QStringLiteral("devices")).toUInt();
        const bool clipboardEnabled =
            results.value(QStringLiteral("clipboard_enabled")).toBool();
        const bool keyboardEnabled = (devices & kKeyboardDevice) != 0;

        step_ = Step::Ready;
        setReadyState(keyboardEnabled, clipboardEnabled);
        setStatus(QStringLiteral("Portal ready · keyboard %1 · clipboard %2")
                  .arg(keyboardEnabled ? QStringLiteral("yes")
                                       : QStringLiteral("no"))
                  .arg(clipboardEnabled ? QStringLiteral("yes")
                                        : QStringLiteral("no")));
    }
}

bool PortalRemoteSession::requestClipboard(QString *error) {
    QDBusInterface iface(kService, kDesktopPath, kClipboard,
                         QDBusConnection::sessionBus());
    const QDBusMessage reply = iface.call(
        QStringLiteral("RequestClipboard"),
        QVariant::fromValue(QDBusObjectPath(sessionPath_)), QVariantMap{});

    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error) *error = reply.errorMessage();
        return false;
    }
    return true;
}

bool PortalRemoteSession::selectDevices(QString *error) {
    QDBusInterface iface(kService, kDesktopPath, kRemoteDesktop,
                         QDBusConnection::sessionBus());

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"),
                   newToken(QStringLiteral("select")));
    options.insert(QStringLiteral("types"), kKeyboardDevice);
    options.insert(QStringLiteral("persist_mode"), 2u);

    QDBusReply<QDBusObjectPath> reply = iface.call(
        QStringLiteral("SelectDevices"),
        QVariant::fromValue(QDBusObjectPath(sessionPath_)), options);
    if (!reply.isValid()) {
        if (error) *error = reply.error().message();
        return false;
    }

    return watchRequest(reply.value().path(), Step::Selecting, error);
}

bool PortalRemoteSession::startSession(QString *error) {
    QDBusInterface iface(kService, kDesktopPath, kRemoteDesktop,
                         QDBusConnection::sessionBus());

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"),
                   newToken(QStringLiteral("start")));

    QDBusReply<QDBusObjectPath> reply = iface.call(
        QStringLiteral("Start"),
        QVariant::fromValue(QDBusObjectPath(sessionPath_)),
        QString(), options);
    if (!reply.isValid()) {
        if (error) *error = reply.error().message();
        return false;
    }

    return watchRequest(reply.value().path(), Step::Starting, error);
}

void PortalRemoteSession::onSessionClosed(const QVariantMap &) {
    sessionPath_.clear();
    fail(QStringLiteral("Portal session closed"));
}

void PortalRemoteSession::onSelectionOwnerChanged(
    const QDBusObjectPath &session, const QVariantMap &options) {
    if (session.path() != sessionPath_ || !clipboardReady_)
        return;

    if (options.value(QStringLiteral("session_is_owner")).toBool())
        return;

    const QStringList mimes =
        options.value(QStringLiteral("mime_types")).toStringList();
    MimePayloads payloads;

    for (const QString &mime : mimes) {
        QString error;
        QByteArray data = readMime(mime, &error);
        if (!data.isNull())
            payloads.insert(mime, std::move(data));
        else
            qWarning() << "Portal clipboard read failed" << mime << error;
    }

    if (!payloads.isEmpty())
        emit clipboardCaptured(payloads);
}

QByteArray PortalRemoteSession::readMime(
    const QString &mimeType, QString *error) {
    QDBusInterface iface(kService, kDesktopPath, kClipboard,
                         QDBusConnection::sessionBus());

    QDBusReply<QDBusUnixFileDescriptor> reply = iface.call(
        QStringLiteral("SelectionRead"),
        QVariant::fromValue(QDBusObjectPath(sessionPath_)), mimeType);
    if (!reply.isValid()) {
        if (error) *error = reply.error().message();
        return {};
    }

    const int fd = ::dup(reply.value().fileDescriptor());
    if (fd < 0) {
        if (error) *error = QString::fromLocal8Bit(strerror(errno));
        return {};
    }

    QByteArray out;
    char buffer[64 * 1024];
    pollfd pfd{fd, POLLIN | POLLHUP, 0};
    bool done = false;    while (!done && out.size() <= kMaxMimeBytes) {
        const int rc = poll(&pfd, 1, kReadTimeoutMs);
        if (rc <= 0) break;

        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            out.append(buffer, n);
        } else if (n == 0) {
            done = true;
        } else if (errno != EINTR) {
            if (error) *error = QString::fromLocal8Bit(strerror(errno));
            out.clear();
            done = true;
        }

        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
            done = true;
    }

    close(fd);
    if (out.size() > kMaxMimeBytes) {
        if (error) *error = QStringLiteral("Clipboard MIME exceeds 64 MiB limit");
        return {};
    }
    return out;
}

bool PortalRemoteSession::setClipboard(
    const MimePayloads &payloads, QString *error) {
    if (!clipboardReady_ || sessionPath_.isEmpty()) {
        if (error) *error = QStringLiteral("Portal clipboard is not ready");
        return false;
    }

    ownedPayloads_ = payloads;
    QVariantMap options;
    options.insert(QStringLiteral("mime_types"),
                   QStringList(payloads.keys()));

    QDBusInterface iface(kService, kDesktopPath, kClipboard,
                         QDBusConnection::sessionBus());
    const QDBusMessage reply = iface.call(
        QStringLiteral("SetSelection"),
        QVariant::fromValue(QDBusObjectPath(sessionPath_)), options);

    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error) *error = reply.errorMessage();
        return false;
    }
    return true;
}

void PortalRemoteSession::onSelectionTransfer(
    const QDBusObjectPath &session, const QString &mimeType, uint serial) {
    if (session.path() != sessionPath_) return;

    QString error;
    const bool ok = writeTransfer(mimeType, serial, &error);
    if (!ok)
        qWarning() << "Portal clipboard write failed" << mimeType << error;
}

bool PortalRemoteSession::writeTransfer(
    const QString &mimeType, uint serial, QString *error) {
    const auto it = ownedPayloads_.constFind(mimeType);
    if (it == ownedPayloads_.cend()) {
        if (error) *error = QStringLiteral("Requested MIME is not owned");
        return false;
    }

    QDBusInterface iface(kService, kDesktopPath, kClipboard,
                         QDBusConnection::sessionBus());
    QDBusReply<QDBusUnixFileDescriptor> reply = iface.call(
        QStringLiteral("SelectionWrite"),
        QVariant::fromValue(QDBusObjectPath(sessionPath_)), serial);

    bool success = false;
    if (reply.isValid()) {
        const int fd = ::dup(reply.value().fileDescriptor());
        if (fd >= 0) {
            const QByteArray &bytes = it.value();
            qsizetype offset = 0;
            while (offset < bytes.size()) {
                const ssize_t n = write(
                    fd, bytes.constData() + offset,
                    static_cast<size_t>(bytes.size() - offset));
                if (n > 0) offset += n;
                else if (n < 0 && errno == EINTR) continue;
                else break;
            }
            success = offset == bytes.size();
            close(fd);
        }
    } else if (error) {
        *error = reply.error().message();
    }

    const QDBusMessage done = iface.call(
        QStringLiteral("SelectionWriteDone"),
        QVariant::fromValue(QDBusObjectPath(sessionPath_)),
        serial, success);

    if (!success && error && error->isEmpty())
        *error = QStringLiteral("Clipboard payload write failed");
    if (done.type() == QDBusMessage::ErrorMessage && error)
        *error = done.errorMessage();
    return success && done.type() != QDBusMessage::ErrorMessage;
}

bool PortalRemoteSession::sendCtrlV(QString *error) {
    if (!ready_ || sessionPath_.isEmpty()) {
        if (error) *error = QStringLiteral("Portal keyboard control is not ready");
        return false;
    }

    QDBusInterface iface(kService, kDesktopPath, kRemoteDesktop,
                         QDBusConnection::sessionBus());
    const QVariant session =
        QVariant::fromValue(QDBusObjectPath(sessionPath_));
    const QVariantMap options;

    const auto send = [&](int keysym, uint state) -> bool {
        const QDBusMessage reply = iface.call(
            QStringLiteral("NotifyKeyboardKeysym"),
            session, options, keysym, state);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            if (error) *error = reply.errorMessage();
            return false;
        }
        return true;
    };

    if (!send(kKeysymControlL, 1)) return false;
    if (!send(kKeysymV, 1)) return false;
    if (!send(kKeysymV, 0)) return false;
    if (!send(kKeysymControlL, 0)) return false;

    setStatus(QStringLiteral("Ctrl+V injected"));
    return true;
}