#include "PortalDbusPasteBackend.h"
#include "PortalRemoteSession.h"

PortalDbusPasteBackend::PortalDbusPasteBackend(
    PortalRemoteSession *session, QObject *parent)
    : PasteBackend(parent), session_(session) {
    if (session_) {
        connect(session_, &PortalRemoteSession::readyChanged,
                this, &PasteBackend::readyChanged);
        connect(session_, &PortalRemoteSession::statusChanged,
                this, &PasteBackend::statusChanged);
    }
}

bool PortalDbusPasteBackend::start(QString *error) {
    if (!session_) {
        if (error) *error = QStringLiteral("Portal session is missing");
        return false;
    }
    return true;
}

bool PortalDbusPasteBackend::paste(QString *error) {
    return session_ && session_->sendCtrlV(error);
}

bool PortalDbusPasteBackend::isReady() const {
    return session_ && session_->isReady();
}

QString PortalDbusPasteBackend::backendName() const {
    return QStringLiteral("XDG RemoteDesktop D-Bus");
}