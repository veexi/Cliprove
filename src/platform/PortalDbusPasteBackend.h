#pragma once

#include "PasteBackend.h"

class PortalRemoteSession;

class PortalDbusPasteBackend final : public PasteBackend {
    Q_OBJECT
public:
    PortalDbusPasteBackend(PortalRemoteSession *session, QObject *parent = nullptr);

    bool start(QString *error = nullptr) override;
    bool paste(QString *error = nullptr) override;
    bool isReady() const override;
    QString backendName() const override;

private:
    PortalRemoteSession *session_;
};