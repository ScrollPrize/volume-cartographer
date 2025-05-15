// CSegmentationEditorWindow.hpp
#pragma once

#include <QMainWindow>
#include <QToolBar>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <opencv2/core/core.hpp>

namespace ChaoVis {

class CVolumeViewer;
class CSurfaceCollection;

class CSegmentationEditorWindow : public QMainWindow
{
    Q_OBJECT

public:
    CSegmentationEditorWindow(CSurfaceCollection* surfCol, QWidget* parent = nullptr);
    ~CSegmentationEditorWindow();

    void setSegmentationSurface(const std::string& name);

private slots:
    void onOffsetChanged(int value);
    void onStepSizeChanged(int value);
    void onMoveForward();
    void onMoveBackward();
    void onReset();

signals:
    void sendShiftNormal(cv::Vec3f step);

private:
    CVolumeViewer* volumeViewer;
    CSurfaceCollection* surfaceCollection;
    std::string surfaceName;
    
    QToolBar* toolBar;
    QSlider* offsetSlider;
    QSlider* stepSizeSlider;
    QLabel* offsetValueLabel;
    QLabel* stepSizeValueLabel;
    QPushButton* moveForwardBtn;
    QPushButton* moveBackwardBtn;
    QPushButton* resetBtn;
    
    float currentOffset = 0.0f;
    float stepSize = 1.0f;
};

} // namespace ChaoVis
