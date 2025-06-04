// CVolumeViewer.cpp
// Chao Du 2015 April
#include "CVolumeViewerView.hpp"

#include <QGraphicsView>
#include <QMouseEvent>
#include <QScrollBar>

using namespace ChaoVis;


CVolumeViewerView::CVolumeViewerView(QWidget* parent) : QGraphicsView(parent)
{ 
    setMouseTracking(true);
};

void CVolumeViewerView::scrollContentsBy(int dx, int dy)
{
    sendScrolled();
    QGraphicsView::scrollContentsBy(dx,dy);
}

void CVolumeViewerView::wheelEvent(QWheelEvent *event)
{
    // Get raw delta value and use smaller divisor for higher sensitivity
    int num_degrees = event->angleDelta().y() / 8;
    
    QPointF global_loc = viewport()->mapFromGlobal(event->globalPosition());
    QPointF scene_loc = mapToScene({int(global_loc.x()),int(global_loc.y())});

    // Send the zoom event with a more sensitive delta value
    // Changed from /15 to /5 to make it more responsive to small wheel movements
    sendZoom(num_degrees/5, scene_loc, event->modifiers());
    
    event->accept();
}

void CVolumeViewerView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)
    {
        setCursor(Qt::ArrowCursor);
        event->accept();
        if (_regular_pan) {
            _regular_pan = false;
            sendPanRelease(event->button(), event->modifiers());
        }
        return;
    }
    else
    {
        QPointF global_loc = viewport()->mapFromGlobal(event->globalPosition());
        QPointF scene_loc = mapToScene({int(global_loc.x()),int(global_loc.y())});
        
        sendVolumeClicked(scene_loc, event->button(), event->modifiers());
        
        event->accept();
        return;
    }
    event->ignore();
}

void CVolumeViewerView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)
    {
        _regular_pan = true;
        _last_pan_position = QPoint(event->position().x(), event->position().y());
        sendPanStart(event->button(), event->modifiers());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    event->ignore();
}

void CVolumeViewerView::mouseMoveEvent(QMouseEvent *event)
{
    if (_regular_pan)
    {
        QPoint scroll = _last_pan_position - QPoint(event->position().x(), event->position().y());
        
        int x = horizontalScrollBar()->value() + scroll.x();
        horizontalScrollBar()->setValue(x);
        int y = verticalScrollBar()->value() + scroll.y();
        verticalScrollBar()->setValue(y);
        
        _last_pan_position = QPoint(event->position().x(), event->position().y());
        event->accept();
        return;
    }
    else {
        QPointF global_loc = viewport()->mapFromGlobal(event->globalPosition());
        QPointF scene_loc = mapToScene({int(global_loc.x()),int(global_loc.y())});
        
        sendCursorMove(scene_loc);
    }
    event->ignore();
}
