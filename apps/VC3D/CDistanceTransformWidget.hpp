#pragma once

#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QFileDialog>
#include <QLineEdit>
#include <QHBoxLayout>
#include <opencv2/core.hpp>
#include <vector>
#include <memory>
#include <map>
#include "PathData.hpp"

namespace volcart {
    class Volume;
    class VolumePkg;
}

class ChunkCache;

namespace ChaoVis {

class CDistanceTransformWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit CDistanceTransformWidget(QWidget* parent = nullptr);
    ~CDistanceTransformWidget();
    
    void setVolumePkg(std::shared_ptr<volcart::VolumePkg> vpkg);
    void setCurrentVolume(std::shared_ptr<volcart::Volume> volume);
    void setCache(ChunkCache* cache);
    
signals:
    void sendPointsChanged(const std::vector<cv::Vec3f> red, const std::vector<cv::Vec3f> blue);
    void sendPathsChanged(const QList<PathData>& paths);
    void sendStatusMessageAvailable(QString text, int timeout);
    
public slots:
    void onPointSelected(cv::Vec3f point, cv::Vec3f normal);
    void onVolumeChanged(std::shared_ptr<volcart::Volume> vol);
    void updateCurrentZSlice(int z);
    void onUserPointAdded(cv::Vec3f point);
    void onMousePress(cv::Vec3f vol_point, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    void onMouseMove(cv::Vec3f vol_point, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
    void onMouseRelease(cv::Vec3f vol_point, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    
private slots:
    void onSetSeedClicked();
    void onCastRaysClicked();
    void onRunSegmentationClicked();
    void onResetPointsClicked();
    void onDrawModeToggled();
    
private:
    // Mode enum
    enum class Mode {
        PointMode,
        DrawMode
    };
    
    void setupUI();
    void computeDistanceTransform();
    void castRays();
    void findPeaksAlongRay(const cv::Vec2f& rayDir, const cv::Vec3f& startPoint);
    void runSegmentation();
    QString findExecutablePath();
    void updateParameterPreview();
    void updateModeUI();
    void analyzePaths();
    void findPeaksAlongPath(const PathData& path);
    void startDrawing(cv::Vec3f startPoint);
    void addPointToPath(cv::Vec3f point);
    void finalizePath();
    QColor generatePathColor();
    void displayPaths();
    
    // UI elements
    QLabel* infoLabel;
    QPushButton* setSeedButton;
    QDoubleSpinBox* angleStepSpinBox;
    QSpinBox* processesSpinBox;
    QSpinBox* thresholdSpinBox;  // Intensity threshold for peak detection
    QSpinBox* windowSizeSpinBox; // Window size for peak detection
    QSpinBox* maxRadiusSpinBox;  // Max radius for ray casting
    
    // Layout and label references for hiding/showing
    QHBoxLayout* maxRadiusLayout;
    QLabel* maxRadiusLabel;
    QHBoxLayout* angleStepLayout;
    QLabel* angleStepLabel;
    
    QString executablePath;
    
    QPushButton* castRaysButton;
    QPushButton* runSegmentationButton;
    QPushButton* resetPointsButton;
    QProgressBar* progressBar;
    
    // Data
    std::shared_ptr<volcart::VolumePkg> fVpkg;
    std::shared_ptr<volcart::Volume> currentVolume;
    ChunkCache* chunkCache;
    cv::Vec3f selectedPoint;
    int currentZSlice;
    std::vector<cv::Vec3f> peakPoints;
    std::vector<cv::Vec3f> userPlacedPoints; // Points placed via shift+click
    cv::Mat distanceTransform;
    bool hasSelectedPoint;
    bool waitingForSeedPoint;
    
    // Drawing mode data
    Mode currentMode;
    QList<PathData> paths;  
    bool isDrawing;
    PathData currentPath;
    int colorIndex;
};

} // namespace ChaoVis
