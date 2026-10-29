#include "windowplacement.h"
#include <QGuiApplication>
#include <QMouseEvent>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <limits>
#include <windows.h>

QRect WindowPlacement::fit(QRect frame, const QList<QRect>& availableAreas) {
    QRect chosen;
    qint64 bestOverlap=-1, bestDistance=std::numeric_limits<qint64>::max();
    for(const auto& area:availableAreas) {
        if(area.isEmpty())continue;
        const auto overlap=area.intersected(frame);
        const qint64 pixels=qint64(overlap.width())*overlap.height();
        const auto point=frame.center();
        const qint64 dx=point.x()-qBound(area.left(),point.x(),area.right());
        const qint64 dy=point.y()-qBound(area.top(),point.y(),area.bottom());
        const auto distance=dx*dx+dy*dy;
        if(pixels>bestOverlap || (pixels==bestOverlap && distance<bestDistance)) {
            chosen=area;bestOverlap=pixels;bestDistance=distance;
        }
    }
    if(chosen.isEmpty())return frame;
    frame.setSize(frame.size().boundedTo(chosen.size()).expandedTo(QSize(1,1)));
    frame.moveLeft(qBound(chosen.left(),frame.left(),chosen.right()-frame.width()+1));
    frame.moveTop(qBound(chosen.top(),frame.top(),chosen.bottom()-frame.height()+1));
    return frame;
}

namespace {
class PlacementGuard final : public QObject {
public:
    explicit PlacementGuard(QWidget* window) : QObject(window),window_(window) {
        timer_.setSingleShot(true);
        connect(&timer_,&QTimer::timeout,this,[this]{adjust();});
        window_->installEventFilter(this);
        for(auto* screen:QGuiApplication::screens())watch(screen);
        connect(qGuiApp,&QGuiApplication::screenAdded,this,[this](QScreen* screen){watch(screen);schedule();});
        connect(qGuiApp,&QGuiApplication::screenRemoved,this,[this]{schedule();});
        // Native handles already exist for the custom chrome and dialog previews.
        window_->winId();
        connect(window_->windowHandle(),&QWindow::screenChanged,this,[this]{schedule();});
    }
private:
    bool eventFilter(QObject*,QEvent* event) override {
        if(event->type()==QEvent::Show)timer_.start(0);
        else if(event->type()==QEvent::WindowStateChange || event->type()==QEvent::DevicePixelRatioChange)schedule();
        return false;
    }
    void watch(QScreen* screen) {
        connect(screen,&QScreen::availableGeometryChanged,this,[this]{schedule();});
        connect(screen,&QScreen::logicalDotsPerInchChanged,this,[this]{schedule();});
    }
    void schedule(){timer_.start(150);}
    void adjust() {
        if(!window_->isVisible() || window_->isMinimized() || window_->isMaximized() || window_->isFullScreen())return;
        // Let native dragging finish before fitting a window on its new screen.
        if(GetAsyncKeyState(VK_LBUTTON)&0x8000){schedule();return;}
        QList<QRect> areas;for(auto* screen:QGuiApplication::screens())areas.append(screen->availableGeometry());
        const auto frame=window_->frameGeometry();
        const auto target=WindowPlacement::fit(frame,areas);
        if(frame==target)return;
        const auto decoration=frame.size()-window_->size();
        window_->resize(target.size()-decoration);
        window_->move(target.topLeft());
    }
    QWidget* window_;
    QTimer timer_;
};
}

void WindowPlacement::keepVisible(QWidget* window) { new PlacementGuard(window); }
