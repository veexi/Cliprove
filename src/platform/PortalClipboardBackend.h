#pragma once

#include "ClipboardBackend.h"

class PortalRemoteSession;

class PortalClipboardBackend final : public ClipboardBackend {
    Q_OBJECT
public:
    PortalClipboardBackend(PortalRemoteSession *session, QObject *parent = nullptr);

    bool start(QString *error = nullptr) override;
    QString backendName() const override;
    bool setClipboard(const MimePayloads &payloads, QString *error = nullptr) override;

private:
    PortalRemoteSession *session_;
};