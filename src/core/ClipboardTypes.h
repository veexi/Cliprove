#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QString>

using MimePayloads = QMap<QString, QByteArray>;

struct ClipboardEntry {
    qint64 id = 0;
    bool starred = false;
    QDateTime createdAt;
    QString summary;
    QString formats;
    QByteArray contentHash;
};

Q_DECLARE_METATYPE(MimePayloads)
