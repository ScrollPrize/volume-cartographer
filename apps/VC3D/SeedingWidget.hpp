#pragma once

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QFileDialog>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QProcess>
#include <QPointer>
#include <opencv2/core.hpp>
#include <vector>
#include <memory>
#include <map>
#include "PathData.hpp"
#include "VCCollection.hpp"

namespace volcart {
    class Volume;
    class VolumePkg;
}

class ChunkCache;

namespace ChaoVis {

class CSurfaceCollection;

class SeedingWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit SeedingWidget(VCCollection* point_collection, CSurfaceCollection* surface_collection, QWidget* parent = nullptr);
    ~SeedingWidget();
    
    void setVolumePkg(std::shared_ptr<volcart::VolumePkg> vpkg);
    void setCurrentVolume(std::shared_ptr<volcart::Volume> volume);
    void setCache(ChunkCache* cache);
    
signals:
    void sendPointsChanged(VCCollection*);
    void sendPathsChanged(const QList<PathData>& paths);
    void sendStatusMessageAvailable(QString text, int timeout);
    
public slots:
    void onSurfacesLoaded();  // Called when surfaces have been loaded/reloaded
    void onCollectionChanged();
    
public slots:
    void onVolumeChanged(std::shared_ptr<volcart::Volume> vol, const std::string& volumeId);
    void updateCurrentZSlice(int z);
    void onMousePress(cv::Vec3f vol_point, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    void onMouseMove(cv::Vec3f vol_point, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
    void onMouseRelease(cv::Vec3f vol_point, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    
private slots:
    void onCastRaysClicked();
    void onRunSegmentationClicked();
    void onExpandSeedsClicked();
    void onResetPointsClicked();
    void onCancelClicked();
    
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
    void updatePointsDisplay();
    void updateInfoLabel();
    void updateButtonStates();
    
private:
    
    // UI elements
    QLabel* infoLabel;
    QComboBox* collectionComboBox;
    QDoubleSpinBox* angleStepSpinBox;
    QSpinBox* processesSpinBox;
    QSpinBox* thresholdSpinBox;  // Intensity threshold for peak detection
    QSpinBox* windowSizeSpinBox; // Window size for peak detection
    QSpinBox* maxRadiusSpinBox;  // Max radius for ray casting
    QSpinBox* expansionIterationsSpinBox; // Number of expansion iterations
    
    // Layout and label references for hiding/showing
    QHBoxLayout* maxRadiusLayout;
    QLabel* maxRadiusLabel;
    QHBoxLayout* angleStepLayout;
    QLabel* angleStepLabel;
    
    QString executablePath;
    
    QPushButton* castRaysButton;
    QPushButton* runSegmentationButton;
    QPushButton* expandSeedsButton;
    QPushButton* resetPointsButton;
    QPushButton* cancelButton;
    QProgressBar* progressBar;
    
    // Data
    std::shared_ptr<volcart::VolumePkg> fVpkg;
    std::shared_ptr<volcart::Volume> currentVolume;
    std::string currentVolumeId;
    ChunkCache* chunkCache;
    int currentZSlice;
    VCCollection* _point_collection;
    CSurfaceCollection* _surface_collection;
    cv::Mat distanceTransform;
    
    // Drawing mode data
    Mode currentMode;
    QList<PathData> paths;  
    bool isDrawing;
    PathData currentPath;
    int colorIndex;
    
    // Process management
    QList<QPointer<QProcess>> runningProcesses;
    bool jobsRunning;
};

} // namespace ChaoVis
