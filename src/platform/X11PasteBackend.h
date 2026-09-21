#pragma once

#include "PasteBackend.h"

struct xcb_connection_t;

class X11PasteBackend final : public PasteBackend {
    Q_OBJECT
public:
    explicit X11PasteBackend(QObject *parent = nullptr);
    ~X11PasteBackend() override;

    bool start(QString *error = nullptr) override;
    bool paste(QString *error = nullptr) override;
    bool isReady() const override;
    QString backendName() const override;

private:
    xcb_connection_t *connection_ = nullptr;
    uint8_t ctrlKeycode_ = 0;
    uint8_t vKeycode_ = 0;
    bool ready_ = false;
};