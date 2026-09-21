#include "PortalClipboardBackend.h"
#include "PortalRemoteSession.h"

PortalClipboardBackend::PortalClipboardBackend(
    PortalRemoteSession *session, QObject *parent)
    : ClipboardBackend(parent), session_(session) {}

bool PortalClipboardBackend::start(QString *error) {
    if (!session_) {
        if (error) *error = QStringLiteral("Portal session is missing");
        return false;
    }

    connect(session_, &PortalRemoteSession::clipboardCaptured,
            this, &ClipboardBackend::clipboardCaptured);
    connect(session_, &PortalRemoteSession::statusChanged,
            this, [this](const QString &message) {
                if (!message.isEmpty())
                    emit backendError(message);
            });
    return true;
}

QString PortalClipboardBackend::backendName() const {
    return QStringLiteral("XDG Clipboard Portal");
}

bool PortalClipboardBackend::setClipboard(
    const MimePayloads &payloads, QString *error) {
    return session_ && session_->setClipboard(payloads, error);
}