/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "PasteBackend.h"
#include <QTimer>

class UinputPasteBackend final : public PasteBackend {
    Q_OBJECT
public:
    explicit UinputPasteBackend(QObject *parent = nullptr);
    ~UinputPasteBackend() override;

    bool start(QString *error = nullptr) override;
    bool paste(QString *error = nullptr) override;
    bool pasteWithShift(bool shift, QString *error = nullptr);
    bool isReady() const override { return ready_; }
    QString backendName() const override { return QStringLiteral("Linux uinput keyboard"); }

private:
    bool writeEvent(unsigned short type, unsigned short code, int value);
    bool key(unsigned short code, bool pressed);
    void resetDevice();

    int fd_ = -1;
    bool created_ = false;
    bool ready_ = false;
    QTimer settleTimer_;
};
