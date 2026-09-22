#include "Theme.h"

#include <QApplication>
#include <QPalette>

namespace Theme {

QIcon appIcon() {
    QIcon icon;
    icon.addFile(QStringLiteral(":/icons/cliprove.png"), QSize(256, 256));
    icon.addFile(QStringLiteral(":/icons/cliprove-64.png"), QSize(64, 64));
    return icon;
}

QIcon minimalIcon() {
    QIcon icon;
    icon.addFile(QStringLiteral(":/icons/cliprove-minimal-16.png"), QSize(16, 16));
    icon.addFile(QStringLiteral(":/icons/cliprove-minimal-20.png"), QSize(20, 20));
    icon.addFile(QStringLiteral(":/icons/cliprove-minimal-24.png"), QSize(24, 24));
    icon.addFile(QStringLiteral(":/icons/cliprove-minimal-32.png"), QSize(32, 32));
    icon.addFile(QStringLiteral(":/icons/cliprove-minimal-48.png"), QSize(48, 48));
    icon.addFile(QStringLiteral(":/icons/cliprove-minimal-64.png"), QSize(64, 64));
    icon.addFile(QStringLiteral(":/icons/cliprove-minimal-128.png"), QSize(128, 128));
    return icon;
}

QIcon windowIcon() {
    QIcon icon;
    icon.addFile(QStringLiteral(":/icons/cliprove-symbolic-16.png"), QSize(16, 16));
    icon.addFile(QStringLiteral(":/icons/cliprove-symbolic-20.png"), QSize(20, 20));
    icon.addFile(QStringLiteral(":/icons/cliprove-symbolic-24.png"), QSize(24, 24));
    icon.addFile(QStringLiteral(":/icons/cliprove-symbolic-32.png"), QSize(32, 32));
    icon.addFile(QStringLiteral(":/icons/cliprove-symbolic-48.png"), QSize(48, 48));
    icon.addFile(QStringLiteral(":/icons/cliprove-symbolic-64.png"), QSize(64, 64));
    icon.addFile(QStringLiteral(":/icons/cliprove-symbolic-128.png"), QSize(128, 128));
    return icon;
}

QString styleSheet() {
    return QStringLiteral(R"QSS(
QWidget {
    font-size: 13px;
}
QMainWindow, QDialog {
    background: palette(window);
}
QWidget#topBar, QWidget#statusBar, QWidget#settingsCard {
    background: palette(base);
    border: 1px solid palette(midlight);
    border-radius: 12px;
}
QLabel#brandTitle {
    font-size: 18px;
    font-weight: 700;
}
QLabel#brandSubtitle, QLabel#secondaryText, QLabel#statusDetail {
    color: palette(mid);
}
QLabel#settingsTitle {
    font-size: 22px;
    font-weight: 700;
}
QLabel#aboutName {
    font-size: 26px;
    font-weight: 750;
}
QLineEdit {
    min-height: 38px;
    padding: 0 12px;
    border: 1px solid palette(midlight);
    border-radius: 10px;
    background: palette(base);
    selection-background-color: #10b981;
}
QLineEdit:focus {
    border-color: #10b981;
}
QPushButton {
    min-height: 36px;
    padding: 0 14px;
    border-radius: 9px;
    border: 1px solid palette(midlight);
    background: palette(base);
}
QPushButton:hover {
    background: palette(alternate-base);
    border-color: #6ee7b7;
}
QPushButton[primary="true"] {
    color: white;
    background: #10b981;
    border-color: #10b981;
    font-weight: 600;
}
QPushButton[primary="true"]:hover {
    background: #059669;
    border-color: #059669;
}
QPushButton#iconButton {
    min-width: 38px;
    max-width: 38px;
    padding: 0;
}
QListWidget#historyList {
    border: 1px solid palette(midlight);
    border-radius: 12px;
    background: palette(base);
    outline: none;
    padding: 6px;
}
QListWidget#historyList::item {
    padding: 0;
    margin: 0;
    border: none;
    background: transparent;
}
QListWidget#historyList::item:hover,
QListWidget#historyList::item:selected {
    background: transparent;
}
QListWidget#settingsNav {
    border: none;
    background: transparent;
    outline: none;
}
QListWidget#settingsNav::item {
    min-height: 38px;
    border-radius: 9px;
    padding: 0 12px;
}
QListWidget#settingsNav::item:hover {
    background: palette(alternate-base);
}
QListWidget#settingsNav::item:selected {
    color: #047857;
    background: rgba(16, 185, 129, 0.13);
    font-weight: 600;
}
QWidget#settingsRow {
    background: transparent;
    border-radius: 9px;
}
QWidget#settingsRow:hover {
    background: palette(alternate-base);
}
QLabel#settingRowTitle {
    font-weight: 600;
}
QSpinBox {
    min-height: 32px;
    padding: 0 8px;
    border: 1px solid palette(midlight);
    border-radius: 8px;
    background: palette(base);
}
QMenu {
    border: 1px solid palette(midlight);
    border-radius: 8px;
    padding: 6px;
}
QMenu::item {
    padding: 7px 20px;
    border-radius: 6px;
}
QMenu::item:selected {
    background: rgba(16, 185, 129, 0.14);
}
)QSS");
}

void apply(QApplication &app) {
    app.setStyleSheet(styleSheet());
}

}
