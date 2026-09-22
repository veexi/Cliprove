#pragma once

#include <QMainWindow>

class HistoryStore;
class ClipboardBackend;
class PasteBackend;
class QLineEdit;
class QListWidget;
class QLabel;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(HistoryStore *store, ClipboardBackend *backend,
               PasteBackend *pasteBackend, QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    void replayCurrent();
    void toggleStarred();

    HistoryStore *store_;
    ClipboardBackend *backend_;
    PasteBackend *pasteBackend_;
    QLineEdit *search_;
    QListWidget *list_;
    QLabel *status_;
};
