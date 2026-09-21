#pragma once

#include <QObject>
#include "core/ClipboardTypes.h"

class ClipboardBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ClipboardBackend() override = default;

    virtual bool start(QString *error = nullptr) = 0;
    virtual QString backendName() const = 0;
    virtual bool setClipboard(const MimePayloads &payloads, QString *error = nullptr) = 0;

signals:
    void clipboardCaptured(const MimePayloads &payloads);
    void backendError(const QString &message);
};