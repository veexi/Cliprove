#include "HistoryStore.h"

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QSqlRecord>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

HistoryStore::HistoryStore(QObject *parent) : QObject(parent) {}

HistoryStore::~HistoryStore() {
    if (db_.isOpen())
        db_.close();
    const QString connectionName = db_.connectionName();
    db_ = QSqlDatabase();
    if (!connectionName.isEmpty())
        QSqlDatabase::removeDatabase(connectionName);
}

bool HistoryStore::open(QString *error) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(base)) {
        if (error) *error = QStringLiteral("Could not create history directory: %1").arg(base);
        return false;
    }

    db_ = QSqlDatabase::addDatabase("QSQLITE", "cliptool-history");
    db_.setDatabaseName(base + "/history.sqlite3");
    if (!db_.open()) {
        if (error) *error = db_.lastError().text();
        return false;
    }

    QSqlQuery q(db_);
    if (!q.exec("PRAGMA journal_mode=WAL") ||
        !q.exec("PRAGMA synchronous=FULL") ||
        !q.exec("PRAGMA busy_timeout=5000") ||
        !q.exec("PRAGMA foreign_keys=ON") ||
        !q.exec("CREATE TABLE IF NOT EXISTS entries ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "created_at INTEGER NOT NULL,"
                "summary TEXT NOT NULL,"
                "formats TEXT NOT NULL,"
                "content_hash BLOB NOT NULL,"
                "starred INTEGER NOT NULL DEFAULT 0)") ||
        !q.exec("CREATE INDEX IF NOT EXISTS idx_entries_created ON entries(created_at DESC)") ||
        !q.exec("CREATE INDEX IF NOT EXISTS idx_entries_hash ON entries(content_hash)") ||
        !q.exec("CREATE TABLE IF NOT EXISTS payloads ("
                "entry_id INTEGER NOT NULL,"
                "mime TEXT NOT NULL,"
                "data BLOB NOT NULL,"
                "PRIMARY KEY(entry_id,mime),"
                "FOREIGN KEY(entry_id) REFERENCES entries(id) ON DELETE CASCADE)") ||
        !q.exec("CREATE TABLE IF NOT EXISTS thumbnails ("
                "entry_id INTEGER PRIMARY KEY,"
                "png BLOB NOT NULL,"
                "FOREIGN KEY(entry_id) REFERENCES entries(id) ON DELETE CASCADE)")) {
        if (error) *error = q.lastError().text();
        return false;
    }

    if (db_.record(QStringLiteral("entries")).indexOf(QStringLiteral("starred")) < 0 &&
        !q.exec("ALTER TABLE entries ADD COLUMN starred INTEGER NOT NULL DEFAULT 0")) {
        if (error) *error = q.lastError().text();
        return false;
    }
    if (!q.exec("PRAGMA quick_check(1)") || !q.next() ||
        q.value(0).toString() != QStringLiteral("ok")) {
        if (error) *error = q.lastError().text().isEmpty()
            ? QStringLiteral("History database integrity check failed") : q.lastError().text();
        return false;
    }
    return true;
}

QByteArray HistoryStore::makeHash(const MimePayloads &payloads) const {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (auto it = payloads.cbegin(); it != payloads.cend(); ++it) {
        hash.addData(it.key().toUtf8());
        hash.addData(QByteArrayView("\0", 1));
        hash.addData(it.value());
        hash.addData(QByteArrayView("\0", 1));
    }
    return hash.result();
}

QString HistoryStore::makeSummary(const MimePayloads &payloads) const {
    for (const QString &mime : {QStringLiteral("text/plain;charset=utf-8"),
                                QStringLiteral("text/plain"),
                                QStringLiteral("UTF8_STRING")}) {
        const auto it = payloads.constFind(mime);
        if (it != payloads.cend()) {
            QString text = QString::fromUtf8(it.value()).simplified();
            if (text.size() > 120) text = text.left(117) + "...";
            if (!text.isEmpty()) return text;
        }
    }

    const auto uriIt = payloads.constFind("text/uri-list");
    if (uriIt != payloads.cend()) {
        QStringList uris;
        for (const QString &line : QString::fromUtf8(uriIt.value()).split('\n')) {
            const QString uri = line.trimmed();
            if (!uri.isEmpty() && !uri.startsWith('#')) uris.append(uri);
        }
        if (!uris.isEmpty()) {
            if (uris.size() == 1)
                return QFileInfo(QUrl::fromEncoded(uris.first().toUtf8()).toLocalFile()).fileName();
            return QString("%1 files").arg(uris.size());
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
    if (!existing.exec()) {
        if (error) *error = existing.lastError().text();
        return -1;
    }
    if (existing.next())
        return existing.value(0).toLongLong();
    existing.finish();

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
    if (maxUnstarredEntries_ > 0 && !pruneUnstarred(maxUnstarredEntries_, error)) {
        db_.rollback();
        return -1;
    }
    if (!db_.commit()) {
        if (error) *error = db_.lastError().text();
        db_.rollback();
        return -1;
    }
    return id;
}

QVector<ClipboardEntry> HistoryStore::recentEntries(int limit, const QString &search,
                                                     QString *error) const {
    QVector<ClipboardEntry> out;
    QSqlQuery q(db_);
    if (search.trimmed().isEmpty()) {
        q.prepare("SELECT id,created_at,summary,formats,content_hash,starred FROM entries "
                  "ORDER BY starred DESC,id DESC LIMIT ?");
        q.addBindValue(limit);
    } else {
        q.prepare("SELECT id,created_at,summary,formats,content_hash,starred FROM entries "
                  "WHERE summary LIKE ? OR formats LIKE ? "
                  "ORDER BY starred DESC,id DESC LIMIT ?");
        const QString term = "%" + search.trimmed() + "%";
        q.addBindValue(term);
        q.addBindValue(term);
        q.addBindValue(limit);
    }
    if (!q.exec()) {
        if (error) *error = q.lastError().text();
        return out;
    }

    while (q.next()) {
        ClipboardEntry e;
        e.id = q.value(0).toLongLong();
        e.createdAt = QDateTime::fromMSecsSinceEpoch(q.value(1).toLongLong());
        e.summary = q.value(2).toString();
        e.formats = q.value(3).toString();
        e.contentHash = q.value(4).toByteArray();
        e.starred = q.value(5).toBool();
        out.push_back(std::move(e));
    }
    if (q.lastError().isValid() && error) *error = q.lastError().text();
    return out;
}

bool HistoryStore::setStarred(qint64 entryId, bool starred, QString *error) {
    if (!db_.transaction()) {
        if (error) *error = db_.lastError().text();
        return false;
    }
    QSqlQuery q(db_);
    q.prepare("UPDATE entries SET starred=? WHERE id=?");
    q.addBindValue(starred ? 1 : 0);
    q.addBindValue(entryId);
    if (!q.exec() || q.numRowsAffected() != 1) {
        if (error) *error = q.lastError().text().isEmpty()
            ? QStringLiteral("History item not found") : q.lastError().text();
        db_.rollback();
        return false;
    }
    q.finish();
    if (!starred && maxUnstarredEntries_ > 0 &&
        !pruneUnstarred(maxUnstarredEntries_, error)) {
        db_.rollback();
        return false;
    }
    if (!db_.commit()) {
        if (error) *error = db_.lastError().text();
        db_.rollback();
        return false;
    }
    return true;
}

bool HistoryStore::pruneUnstarred(int limit, QString *error) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM entries WHERE id IN ("
              "SELECT id FROM entries WHERE starred=0 ORDER BY id DESC LIMIT -1 OFFSET ?)");
    q.addBindValue(limit);
    if (!q.exec()) {
        if (error) *error = q.lastError().text();
        return false;
    }
    return true;
}

bool HistoryStore::setMaxUnstarredEntries(int limit, QString *error) {
    if (limit < 0 || limit > 100000) {
        if (error) *error = QStringLiteral("History limit must be between 0 and 100000");
        return false;
    }
    if (limit > 0) {
        if (!db_.transaction()) {
            if (error) *error = db_.lastError().text();
            return false;
        }
        if (!pruneUnstarred(limit, error) || !db_.commit()) {
            if (error && error->isEmpty()) *error = db_.lastError().text();
            db_.rollback();
            return false;
        }
    }
    maxUnstarredEntries_ = limit;
    return true;
}

QImage HistoryStore::thumbnailFor(qint64 entryId) const {
    if (const auto *cached = thumbnailCache_.object(entryId)) return *cached;

    QSqlQuery cached(db_);
    cached.prepare("SELECT png FROM thumbnails WHERE entry_id=?");
    cached.addBindValue(entryId);
    if (cached.exec() && cached.next()) {
        QImage image = QImage::fromData(cached.value(0).toByteArray(), "PNG");
        if (!image.isNull()) {
            thumbnailCache_.insert(entryId, new QImage(image),
                                   std::max(1, int(image.sizeInBytes() / 1024)));
            return image;
        }
    }
    cached.finish();

    const auto remember = [this, entryId](const QImage &image) {
        QByteArray png;
        QBuffer output(&png);
        output.open(QIODevice::WriteOnly);
        if (image.save(&output, "PNG")) {
            QSqlQuery save(db_);
            save.prepare("INSERT OR REPLACE INTO thumbnails(entry_id,png) VALUES(?,?)");
            save.addBindValue(entryId);
            save.addBindValue(png);
            save.exec();
        }
        thumbnailCache_.insert(entryId, new QImage(image),
                               std::max(1, int(image.sizeInBytes() / 1024)));
        return image;
    };

    const auto readSmallImage = [](QImageReader &reader) {
        const QSize original = reader.size();
        if (!original.isValid() ||
            qint64(original.width()) * original.height() > 20000000) return QImage{};
        if (original.width() > 128 || original.height() > 128)
            reader.setScaledSize(original.scaled(128, 128, Qt::KeepAspectRatio));
        QImage image = reader.read();
        if (image.width() > 128 || image.height() > 128)
            image = image.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return image;
    };

    QSqlQuery source(db_);
    source.prepare("SELECT data FROM payloads WHERE entry_id=? AND length(data)<=33554432 AND mime IN "
                   "('image/png','image/jpeg','image/webp','image/gif','image/bmp') "
                   "ORDER BY CASE mime WHEN 'image/png' THEN 0 WHEN 'image/jpeg' THEN 1 "
                   "WHEN 'image/webp' THEN 2 WHEN 'image/gif' THEN 3 ELSE 4 END");
    source.addBindValue(entryId);
    if (!source.exec()) return {};
    while (source.next()) {
        const QByteArray bytes = source.value(0).toByteArray();
        if (bytes.isEmpty() || bytes.size() > 32 * 1024 * 1024) continue;
        QBuffer buffer;
        buffer.setData(bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        QImage image = readSmallImage(reader);
        if (image.isNull()) continue;
        source.finish();
        return remember(image);
    }

    QSqlQuery files(db_);
    files.prepare("SELECT data FROM payloads WHERE entry_id=? AND mime='text/uri-list'");
    files.addBindValue(entryId);
    if (!files.exec() || !files.next()) return {};
    const auto lines = files.value(0).toByteArray().split('\n');
    files.finish();
    for (const QByteArray &rawLine : lines) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QUrl url = QUrl::fromEncoded(line);
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        const QFileInfo file(path);
        const QString extension = file.suffix().toLower();
        if (!file.isFile() || file.size() > 32 * 1024 * 1024 ||
            (extension != QStringLiteral("png") && extension != QStringLiteral("jpg") &&
             extension != QStringLiteral("jpeg") && extension != QStringLiteral("webp") &&
             extension != QStringLiteral("gif") && extension != QStringLiteral("bmp"))) continue;
        QImageReader reader(path);
        const QImage image = readSmallImage(reader);
        if (!image.isNull()) return remember(image);
    }
    return {};
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
