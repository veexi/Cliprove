/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QObject>
#include <QVariantList>
#include <functional>
#include "../core/ClipboardTypes.h"

class PasteController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
public:
    explicit PasteController(QObject *parent = nullptr) : QObject(parent) {}
    bool busy() const { return busy_; }
    QString error() const { return error_; }
    // The popup supplies a compositor-backed check for the original window.
    std::function<bool()> targetIsActive;
    std::function<bool(const MimePayloads &, QString *)> publishPayload;
    void clipboardRead(const QString &mime);
    Q_INVOKABLE void paste(QObject *history, const QVariantList &uuids, bool shift);
    void cancel();
    static bool usesShift(const QString &appId);
signals:
    void stateChanged();
    void hideRequested();
    void failed();
    void completed();
private:
    void fail(const QString &message);
    void hideWhenReleased(quint64 request, bool shift, int attempts = 0);
    void injectWhenFocused(quint64 request, bool shift, int attempts = 0);
    void advanceQueue(quint64 request, bool shift, int attempts = 0);
    bool publishCurrent();
    bool busy_ = false;
    QString error_;
    quint64 generation_ = 0;
    QList<MimePayloads> queue_;
    qsizetype queueIndex_ = 0;
    bool readArmed_ = false;
    bool payloadRead_ = false;
};
