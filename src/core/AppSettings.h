#pragma once

#include <QSettings>
#include <algorithm>

struct AppSettings {
    int batchIntervalMs = 700;
    int maxUnstarredEntries = 0;
    int thumbnailSize = 72;
    bool showThumbnails = true;
    bool clickToPaste = true;
    bool animationsEnabled = true;
    bool compactRows = false;
    bool startMinimized = false;
    bool closeToTray = true;

    static AppSettings load() {
        QSettings settings;
        AppSettings result;
        result.batchIntervalMs = std::clamp(
            settings.value(QStringLiteral("paste/batchIntervalMs"), 700).toInt(), 200, 5000);
        result.maxUnstarredEntries = std::clamp(
            settings.value(QStringLiteral("history/maxUnstarredEntries"), 0).toInt(), 0, 100000);
        result.thumbnailSize = std::clamp(
            settings.value(QStringLiteral("ui/thumbnailSize"), 72).toInt(), 40, 128);
        result.showThumbnails = settings.value(QStringLiteral("ui/showThumbnails"), true).toBool();
        result.clickToPaste = settings.value(QStringLiteral("paste/clickToPaste"), true).toBool();
        result.animationsEnabled = settings.value(QStringLiteral("ui/animationsEnabled"), true).toBool();
        result.compactRows = settings.value(QStringLiteral("ui/compactRows"), false).toBool();
        result.startMinimized = settings.value(QStringLiteral("general/startMinimized"), false).toBool();
        result.closeToTray = settings.value(QStringLiteral("general/closeToTray"), true).toBool();
        return result;
    }

    bool save() const {
        QSettings settings;
        settings.setValue(QStringLiteral("paste/batchIntervalMs"), batchIntervalMs);
        settings.setValue(QStringLiteral("history/maxUnstarredEntries"), maxUnstarredEntries);
        settings.setValue(QStringLiteral("ui/thumbnailSize"), thumbnailSize);
        settings.setValue(QStringLiteral("ui/showThumbnails"), showThumbnails);
        settings.setValue(QStringLiteral("paste/clickToPaste"), clickToPaste);
        settings.setValue(QStringLiteral("ui/animationsEnabled"), animationsEnabled);
        settings.setValue(QStringLiteral("ui/compactRows"), compactRows);
        settings.setValue(QStringLiteral("general/startMinimized"), startMinimized);
        settings.setValue(QStringLiteral("general/closeToTray"), closeToTray);
        settings.sync();
        return settings.status() == QSettings::NoError;
    }
};
