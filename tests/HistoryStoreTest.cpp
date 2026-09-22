#include "core/HistoryStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDebug>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("veexi-test"));
    QCoreApplication::setApplicationName(QStringLiteral("Cliprove-history-test"));

    QTemporaryDir dataDir;
    if (!dataDir.isValid()) return 1;
    qputenv("XDG_DATA_HOME", dataDir.path().toUtf8());
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
    }
    qInfo() << "History migration, stars, persistence, ordering, deduplication and payload: OK";
    return 0;
}
