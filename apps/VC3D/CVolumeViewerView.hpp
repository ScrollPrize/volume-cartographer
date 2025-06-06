// CVolumeViewer.cpp
// Chao Du 2015 April
#include <QGraphicsView>

namespace ChaoVis
{

class CVolumeViewerView : public QGraphicsView
{
    Q_OBJECT
    
public:
    CVolumeViewerView(QWidget* parent = 0);
    void mouseReleaseEvent(QMouseEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);
    void scrollContentsBy(int dx, int dy);
    void keyPressEvent(QKeyEvent *event);
    
signals:
    void sendScrolled();
    void sendZoom(int steps, QPointF scene_point, Qt::KeyboardModifiers);
    void sendVolumeClicked(QPointF, Qt::MouseButton, Qt::KeyboardModifiers);
    void sendPanRelease(Qt::MouseButton, Qt::KeyboardModifiers);
    void sendPanStart(Qt::MouseButton, Qt::KeyboardModifiers);
    void sendCursorMove(QPointF);
    void sendMousePress(QPointF, Qt::MouseButton, Qt::KeyboardModifiers);
    void sendMouseMove(QPointF, Qt::MouseButtons, Qt::KeyboardModifiers);
    void sendMouseRelease(QPointF, Qt::MouseButton, Qt::KeyboardModifiers);
    
protected:
    bool _regular_pan = false;
    QPoint _last_pan_position;
    bool _left_button_pressed = false;
};

}
