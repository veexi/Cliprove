#pragma once

#include "PasteBackend.h"

struct oeffis;
struct ei;
struct ei_device;

class QSocketNotifier;

class PortalEisPasteBackend final : public PasteBackend {
    Q_OBJECT
public:
    explicit PortalEisPasteBackend(QObject *parent = nullptr);
    ~PortalEisPasteBackend() override;

    bool start(QString *error = nullptr) override;
    bool paste(QString *error = nullptr) override;
    bool isReady() const override;
    QString backendName() const override;

private:
    void onOeffisReadable();
    void onEiReadable();
    void processEiEvents();
    void setReady(bool ready);
    void resetEi();
    bool sendCtrlV(QString *error);

    oeffis *portal_ = nullptr;
    ei *ei_ = nullptr;
    ei_device *keyboard_ = nullptr;
    QSocketNotifier *portalNotifier_ = nullptr;
    QSocketNotifier *eiNotifier_ = nullptr;
    bool started_ = false;
    bool ready_ = false;
    bool keyboardResumed_ = false;
    uint32_t sequence_ = 1;
};