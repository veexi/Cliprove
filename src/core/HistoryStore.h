#pragma once

#include <QObject>
#include <QCache>
#include <QImage>
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
    QVector<ClipboardEntry> recentEntries(int limit = 1000, const QString &search = {},
                                          QString *error = nullptr) const;
    MimePayloads payloadsFor(qint64 entryId) const;
    bool setStarred(qint64 entryId, bool starred, QString *error = nullptr);
    QImage thumbnailFor(qint64 entryId) const;
    bool setMaxUnstarredEntries(int limit, QString *error = nullptr);

private:
    QString makeSummary(const MimePayloads &payloads) const;
    QByteArray makeHash(const MimePayloads &payloads) const;
    bool pruneUnstarred(int limit, QString *error = nullptr);
    QSqlDatabase db_;
    int maxUnstarredEntries_ = 0;
    mutable QCache<qint64, QImage> thumbnailCache_{16384};
};
