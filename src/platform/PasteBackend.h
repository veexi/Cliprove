#pragma once

#include <QObject>
#include <QString>

class PasteBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~PasteBackend() override = default;

    virtual bool start(QString *error = nullptr) = 0;
    virtual bool paste(QString *error = nullptr) = 0;
    virtual bool isReady() const = 0;
    virtual QString backendName() const = 0;

signals:
    void readyChanged(bool ready);
    void statusChanged(const QString &message);
};