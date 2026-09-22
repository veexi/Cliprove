#pragma once

#include <QDialog>
#include "core/AppSettings.h"

class HistoryStore;
class QListWidget;
class QStackedWidget;
class QSpinBox;
class QLabel;
class ToggleSwitch;

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const AppSettings &settings, HistoryStore *store,
                   QWidget *parent = nullptr);

    AppSettings settings() const;

private:
    QWidget *makeGeneralPage();
    QWidget *makePastePage();
    QWidget *makeHistoryPage();
    QWidget *makeAppearancePage();
    QWidget *makeAboutPage();
    QWidget *makePage(const QString &title, const QString &subtitle,
                      QWidget *content);
    QWidget *makeToggleRow(const QString &title, const QString &description,
                           ToggleSwitch *toggle, QWidget *parent);
    QWidget *makeControlRow(const QString &title, const QString &description,
                            QWidget *control, QWidget *parent);
    void switchPage(int index, bool animate = true);
    void updateHistoryHint();

    AppSettings original_;
    HistoryStore *store_ = nullptr;
    QListWidget *nav_ = nullptr;
    QStackedWidget *pages_ = nullptr;

    QSpinBox *batchInterval_ = nullptr;
    QSpinBox *historyLimit_ = nullptr;
    QSpinBox *thumbnailSize_ = nullptr;
    ToggleSwitch *showThumbnails_ = nullptr;
    ToggleSwitch *clickToPaste_ = nullptr;
    ToggleSwitch *animationsEnabled_ = nullptr;
    ToggleSwitch *compactRows_ = nullptr;
    ToggleSwitch *startMinimized_ = nullptr;
    ToggleSwitch *closeToTray_ = nullptr;
    QLabel *historyHint_ = nullptr;
};