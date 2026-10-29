#pragma once
#include <QList>
#include <QRect>

class QWidget;

namespace WindowPlacement {
// Work in Qt's device-independent coordinates, treating each screen separately.
QRect fit(QRect frame, const QList<QRect>& availableAreas);
void keepVisible(QWidget* window);
}
