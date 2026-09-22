#include "core/AppSettings.h"
#include "core/HistoryStore.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QDebug>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("veexi-test"));
    QCoreApplication::setApplicationName(QStringLiteral("Cliprove-history-test"));

    QTemporaryDir dataDir;
    if (!dataDir.isValid()) return 1;
    qputenv("XDG_DATA_HOME", dataDir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dataDir.path().toUtf8());
    const QString databasePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                 + QStringLiteral("/history.sqlite3");
    if (!QDir().mkpath(QFileInfo(databasePath).absolutePath())) return 2;

    {
        auto legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("legacy-test"));
        legacy.setDatabaseName(databasePath);
        if (!legacy.open()) {
            std::fprintf(stderr, "SQLite open failed: %s: %s\n",
                         qPrintable(databasePath), qPrintable(legacy.lastError().text()));
            return 3;
        }
        QSqlQuery q(legacy);
        if (!q.exec("CREATE TABLE entries (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "created_at INTEGER NOT NULL,summary TEXT NOT NULL,"
                    "formats TEXT NOT NULL,content_hash BLOB NOT NULL)")) return 4;
        q.finish();
        legacy.close();
    }

    QString error;
    qint64 first = 0;
    {
        HistoryStore store;
        if (!store.open(&error)) { qCritical() << error; return 5; }
        const MimePayloads firstPayload{{QStringLiteral("text/plain"), "first"}};
        const MimePayloads secondPayload{{QStringLiteral("text/plain"), "second"}};
        first = store.addEntry(firstPayload, &error);
        const qint64 second = store.addEntry(secondPayload, &error);
        if (first <= 0 || second <= first) return 6;
        if (!store.setStarred(first, true, &error)) return 7;
        auto entries = store.recentEntries();
        if (entries.size() != 2 || entries[0].id != first || !entries[0].starred) return 8;
        if (store.addEntry(firstPayload, &error) != first) return 9;
        if (!store.setStarred(first, false, &error)) return 10;
        entries = store.recentEntries();
        if (entries[0].id != second || entries[1].starred) return 11;
        if (store.payloadsFor(first) != firstPayload) return 12;
        if (!store.setStarred(first, true, &error)) return 13;
    }
    {
        HistoryStore reopened;
        if (!reopened.open(&error)) return 14;
        const auto entries = reopened.recentEntries();
        if (entries.size() != 2 || entries[0].id != first || !entries[0].starred) return 15;

        if (!reopened.setMaxUnstarredEntries(1, &error)) return 16;
        const qint64 newest = reopened.addEntry(
            {{QStringLiteral("text/plain"), "newest"}}, &error);
        if (newest <= first) return 17;
        const auto retained = reopened.recentEntries();
        if (retained.size() != 2 || retained[0].id != first || retained[1].id != newest) return 18;
        if (!reopened.setStarred(first, false, &error)) return 19;
        const auto afterUnstar = reopened.recentEntries();
        if (afterUnstar.size() != 1 || afterUnstar[0].id != newest) return 20;
        if (!reopened.payloadsFor(first).isEmpty()) return 26;
        if (!reopened.setMaxUnstarredEntries(0, &error)) return 21;

        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer output(&png);
        output.open(QIODevice::WriteOnly);
        if (!image.save(&output, "PNG")) return 22;
        const qint64 imageId = reopened.addEntry(
            {{QStringLiteral("image/png"), png}}, &error);
        const QImage embeddedThumbnail = reopened.thumbnailFor(imageId);
        if (embeddedThumbnail.isNull() || embeddedThumbnail.width() > 128) return 23;

        const QString filePath = dataDir.path() + QStringLiteral("/sample.png");
        if (!image.save(filePath, "PNG")) return 24;
        const qint64 uriId = reopened.addEntry(
            {{QStringLiteral("text/uri-list"), QUrl::fromLocalFile(filePath).toEncoded()}}, &error);
        const QImage fileThumbnail = reopened.thumbnailFor(uriId);
        if (fileThumbnail.isNull() || fileThumbnail.height() > 128) return 25;
    }
    AppSettings settings;
    settings.batchIntervalMs = 1200;
    settings.maxUnstarredEntries = 12;
    settings.showThumbnails = false;
    if (!settings.save()) return 27;
    const AppSettings loaded = AppSettings::load();
    if (loaded.batchIntervalMs != 1200 || loaded.maxUnstarredEntries != 12 ||
        loaded.showThumbnails) return 28;
    qInfo() << "History migration, retention, stars, thumbnails and persistence: OK";
    return 0;
}
