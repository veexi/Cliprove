#include "HistoryStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

HistoryStore::HistoryStore(QObject *parent) : QObject(parent) {}

HistoryStore::~HistoryStore() {
    if (db_.isOpen())
        db_.close();
}

bool HistoryStore::open(QString *error) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);

    db_ = QSqlDatabase::addDatabase("QSQLITE", "cliptool-history");
    db_.setDatabaseName(base + "/history.sqlite3");
    if (!db_.open()) {
        if (error) *error = db_.lastError().text();
        return false;
    }

    QSqlQuery q(db_);
    if (!q.exec("PRAGMA journal_mode=WAL") ||
        !q.exec("PRAGMA foreign_keys=ON") ||
        !q.exec("CREATE TABLE IF NOT EXISTS entries ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "created_at INTEGER NOT NULL,"
                "summary TEXT NOT NULL,"
                "formats TEXT NOT NULL,"
                "content_hash BLOB NOT NULL)") ||
        !q.exec("CREATE INDEX IF NOT EXISTS idx_entries_created ON entries(created_at DESC)") ||
        !q.exec("CREATE INDEX IF NOT EXISTS idx_entries_hash ON entries(content_hash)") ||
        !q.exec("CREATE TABLE IF NOT EXISTS payloads ("
                "entry_id INTEGER NOT NULL,"
                "mime TEXT NOT NULL,"
                "data BLOB NOT NULL,"
                "PRIMARY KEY(entry_id,mime),"
                "FOREIGN KEY(entry_id) REFERENCES entries(id) ON DELETE CASCADE)")) {
        if (error) *error = q.lastError().text();
        return false;
    }
    return true;
}

QByteArray HistoryStore::makeHash(const MimePayloads &payloads) const {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (auto it = payloads.cbegin(); it != payloads.cend(); ++it) {
        hash.addData(it.key().toUtf8());
        hash.addData("\0", 1);
        hash.addData(it.value());
        hash.addData("\0", 1);
    }
    return hash.result();
}

QString HistoryStore::makeSummary(const MimePayloads &payloads) const {
    for (const QString &mime : {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING"}) {
        const auto it = payloads.constFind(mime);
        if (it != payloads.cend()) {
            QString text = QString::fromUtf8(it.value()).simplified();
            if (text.size() > 120) text = text.left(117) + "...";
            if (!text.isEmpty()) return text;
        }
    }

    const auto uriIt = payloads.constFind("text/uri-list");
    if (uriIt != payloads.cend()) {
        const QStringList lines = QString::fromUtf8(uriIt.value()).split('\n', Qt::SkipEmptyParts);
        if (!lines.isEmpty()) {
            if (lines.size() == 1) return QFileInfo(QUrl(lines.first().trimmed()).toLocalFile()).fileName();
            return QString("%1 files").arg(lines.size());
        }
    }

    for (auto it = payloads.cbegin(); it != payloads.cend(); ++it)
        if (it.key().startsWith("image/"))
            return QString("Image · %1").arg(it.key());

    return payloads.isEmpty() ? QString("Empty clipboard")
                              : QString("%1 format(s)").arg(payloads.size());
}

qint64 HistoryStore::addEntry(const MimePayloads &payloads, QString *error) {
    if (payloads.isEmpty()) return 0;
    const QByteArray hash = makeHash(payloads);

    QSqlQuery existing(db_);
    existing.prepare("SELECT id FROM entries WHERE content_hash=? ORDER BY id DESC LIMIT 1");
    existing.addBindValue(hash);
    if (existing.exec() && existing.next())
        return existing.value(0).toLongLong();

    if (!db_.transaction()) {
        if (error) *error = db_.lastError().text();
        return -1;
    }

    QSqlQuery entry(db_);
    entry.prepare("INSERT INTO entries(created_at,summary,formats,content_hash) VALUES(?,?,?,?)");
    entry.addBindValue(QDateTime::currentMSecsSinceEpoch());
    entry.addBindValue(makeSummary(payloads));
    entry.addBindValue(QStringList(payloads.keys()).join("\n"));
    entry.addBindValue(hash);
    if (!entry.exec()) {
        db_.rollback();
        if (error) *error = entry.lastError().text();
        return -1;
    }

    const qint64 id = entry.lastInsertId().toLongLong();
    QSqlQuery p(db_);
    p.prepare("INSERT INTO payloads(entry_id,mime,data) VALUES(?,?,?)");
    for (auto it = payloads.cbegin(); it != payloads.cend(); ++it) {
        p.bindValue(0, id);
        p.bindValue(1, it.key());
        p.bindValue(2, it.value());
        if (!p.exec()) {
            db_.rollback();
            if (error) *error = p.lastError().text();
            return -1;
        }
    }
    if (!db_.commit()) {
        if (error) *error = db_.lastError().text();
        return -1;
    }
    return id;
}

QVector<ClipboardEntry> HistoryStore::recentEntries(int limit, const QString &search) const {
    QVector<ClipboardEntry> out;
    QSqlQuery q(db_);
    if (search.trimmed().isEmpty()) {
        q.prepare("SELECT id,created_at,summary,formats,content_hash FROM entries "
                  "ORDER BY id DESC LIMIT ?");
        q.addBindValue(limit);
    } else {
        q.prepare("SELECT id,created_at,summary,formats,content_hash FROM entries "
                  "WHERE summary LIKE ? OR formats LIKE ? ORDER BY id DESC LIMIT ?");
        const QString term = "%" + search.trimmed() + "%";
        q.addBindValue(term);
        q.addBindValue(term);
        q.addBindValue(limit);
    }
    if (!q.exec()) return out;

    while (q.next()) {
        ClipboardEntry e;
        e.id = q.value(0).toLongLong();
        e.createdAt = QDateTime::fromMSecsSinceEpoch(q.value(1).toLongLong());
        e.summary = q.value(2).toString();
        e.formats = q.value(3).toString();
        e.contentHash = q.value(4).toByteArray();
        out.push_back(std::move(e));
    }
    return out;
}

MimePayloads HistoryStore::payloadsFor(qint64 entryId) const {
    MimePayloads out;
    QSqlQuery q(db_);
    q.prepare("SELECT mime,data FROM payloads WHERE entry_id=? ORDER BY mime");
    q.addBindValue(entryId);
    if (!q.exec()) return out;
    while (q.next())
        out.insert(q.value(0).toString(), q.value(1).toByteArray());
    return out;
}