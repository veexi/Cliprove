#pragma once

#include <QObject>
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVariantMap>
#include <QtDBus/QDBusObjectPath>

#include "core/ClipboardTypes.h"

class PortalRemoteSession final : public QObject {
    Q_OBJECT
public:
    explicit PortalRemoteSession(QObject *parent = nullptr);
    ~PortalRemoteSession() override;

    bool start(QString *error = nullptr);
    bool isReady() const;
    bool clipboardReady() const;
    QString status() const;

    bool setClipboard(const MimePayloads &payloads, QString *error = nullptr);
    bool sendCtrlV(QString *error = nullptr);

signals:
    void readyChanged(bool ready);
    void clipboardReadyChanged(bool ready);
    void clipboardCaptured(const MimePayloads &payloads);
    void statusChanged(const QString &message);

private slots:
    void onRequestResponse(uint response, const QVariantMap &results);
    void onSelectionOwnerChanged(const QDBusObjectPath &session,
                                 const QVariantMap &options);
    void onSelectionTransfer(const QDBusObjectPath &session,
                             const QString &mimeType, uint serial);
    void onSessionClosed(const QVariantMap &details);

private:
    enum class Step { Idle, Creating, Selecting, Starting, Ready, Failed };

    bool createSession(QString *error);
    bool requestClipboard(QString *error);
    bool selectDevices(QString *error);
    bool startSession(QString *error);
    bool watchRequest(const QString &requestPath, Step step, QString *error);
    void clearRequestWatch();
    void setStatus(const QString &status);
    void fail(const QString &message);
    void setReadyState(bool inputReady, bool clipboardReady);
    QByteArray readMime(const QString &mimeType, QString *error);
    bool writeTransfer(const QString &mimeType, uint serial, QString *error);
    QString newToken(const QString &prefix) const;

    Step step_ = Step::Idle;
    QString requestPath_;
    QString sessionPath_;
    QString status_;
    bool ready_ = false;
    bool clipboardReady_ = false;
    MimePayloads ownedPayloads_;
};