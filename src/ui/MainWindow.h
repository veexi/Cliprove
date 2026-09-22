#pragma once

#include <QMainWindow>
#include <QTimer>
#include <QVector>
#include "core/AppSettings.h"

class HistoryStore;
class ClipboardBackend;
class PasteBackend;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QShowEvent;
class QCloseEvent;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(HistoryStore *store, ClipboardBackend *backend,
               PasteBackend *pasteBackend, const AppSettings &settings,
               QWidget *parent = nullptr);

    void onClipboardCaptured();
    void showStoreError(const QString &error);

public slots:
    void refresh();
    void openSettings();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void replayCurrent();
    void applyVisualStyle();
    void animateWindowIn();
    void showStatus(const QString &message, bool error = false);
    void toggleStarred();
    void loadVisibleThumbnails();
    void replayNext();
    void finishReplay(const QString &error = {});

    enum class ReplayStage { Idle, Paste, Next };

    HistoryStore *store_;
    ClipboardBackend *backend_;
    PasteBackend *pasteBackend_;
    QLineEdit *search_;
    QListWidget *list_;
    QLabel *status_;
    QLabel *countLabel_;
    QPushButton *starButton_;
    QPushButton *replayButton_;
    QPushButton *settingsButton_;
    AppSettings settings_;
    bool hasAnimatedShow_ = false;
    QTimer replayTimer_;
    QVector<qint64> replayIds_;
    qsizetype replayIndex_ = 0;
    ReplayStage replayStage_ = ReplayStage::Idle;
    bool loadingThumbnails_ = false;
};
