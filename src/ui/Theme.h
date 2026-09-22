#pragma once

#include <QString>
#include <QIcon>

class QApplication;

namespace Theme {
QString styleSheet();
QIcon appIcon();
QIcon minimalIcon();
QIcon windowIcon();
void apply(QApplication &app);
}
