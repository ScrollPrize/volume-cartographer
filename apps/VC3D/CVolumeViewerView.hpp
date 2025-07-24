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
    /// Set physical voxel size (units per scene-unit, e.g. µm/pixel).
    /// Call this after you load your Zarr spacing metadata.
    void setVoxelSize(double sx, double sy) { m_vx = sx; m_vy = sy; update(); }
    bool isCurvePanKeyPressed() { return curvePanKeyPressed; }
    void showCurrentImpactRange(int range);
    void showCurrentScanRange(int range);
    void showCurrentSliceIndex(int slice, bool highlight);

    void showTextAboveCursor(const QString& value, const QString& label, const QColor& color);
    void hideTextAboveCursor();

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
    bool rangeKeyPressed{false};
    bool curvePanKeyPressed{false};
    bool rotateKeyPressed{false};
    bool _regular_pan = false;
    QPoint _last_pan_position;
    QGraphicsTextItem* textAboveCursor;
    QGraphicsRectItem* backgroundBehindText;
    QTimer* timerTextAboveCursor;
    bool _left_button_pressed = false;
    /// Draw our scalebar on every repaint
    void drawForeground(QPainter* painter, const QRectF& sceneRect) override;

 private:
    /// Round “ideal” length to 1,2 or 5 × 10^n
    double chooseNiceLength(double nominal) const;

    // µm per scene-unit (pixel)  
    double m_vx = 32.0, m_vy = 32.0;
};

}
