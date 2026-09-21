#pragma once

#include "ClipboardBackend.h"

class QClipboard;

class X11ClipboardBackend final : public ClipboardBackend {
    Q_OBJECT
public:
    explicit X11ClipboardBackend(QObject *parent = nullptr);

    bool start(QString *error = nullptr) override;
    QString backendName() const override;
    bool setClipboard(const MimePayloads &payloads, QString *error = nullptr) override;

private:
    void captureClipboard();

    QClipboard *clipboard_ = nullptr;
    bool suppressNextCapture_ = false;
};