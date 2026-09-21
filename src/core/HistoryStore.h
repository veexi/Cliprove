#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QVector>
#include "ClipboardTypes.h"

class HistoryStore : public QObject {
    Q_OBJECT
public:
    explicit HistoryStore(QObject *parent = nullptr);
    ~HistoryStore() override;

    bool open(QString *error = nullptr);
    qint64 addEntry(const MimePayloads &payloads, QString *error = nullptr);
    QVector<ClipboardEntry> recentEntries(int limit = 1000, const QString &search = {}) const;
    MimePayloads payloadsFor(qint64 entryId) const;

private:
    QString makeSummary(const MimePayloads &payloads) const;
    QByteArray makeHash(const MimePayloads &payloads) const;
    QSqlDatabase db_;
};