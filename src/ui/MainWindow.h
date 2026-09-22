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
class QShowEvent;

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

protected:
    void showEvent(QShowEvent *event) override;

private:
    void replayCurrent();
    void toggleStarred();
    void openSettings();
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
    AppSettings settings_;
    QTimer replayTimer_;
    QVector<qint64> replayIds_;
    qsizetype replayIndex_ = 0;
    ReplayStage replayStage_ = ReplayStage::Idle;
    bool loadingThumbnails_ = false;
};
