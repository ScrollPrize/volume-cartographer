#pragma once

#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QFileDialog>
#include <QLineEdit>
#include <opencv2/core.hpp>
#include <vector>
#include <memory>

namespace volcart {
    class Volume;
    class VolumePkg;
}

namespace ChaoVis {

class CDistanceTransformWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit CDistanceTransformWidget(QWidget* parent = nullptr);
    ~CDistanceTransformWidget();
    
    void setVolumePkg(std::shared_ptr<volcart::VolumePkg> vpkg);
    void setCurrentVolume(std::shared_ptr<volcart::Volume> volume);
    
signals:
    void sendPointsChanged(const std::vector<cv::Vec3f> red, const std::vector<cv::Vec3f> blue);
    void sendStatusMessageAvailable(QString text, int timeout);
    
public slots:
    void onPointSelected(cv::Vec3f point, cv::Vec3f normal);
    void onVolumeChanged(std::shared_ptr<volcart::Volume> vol);
    void updateCurrentZSlice(int z);
    
private slots:
    void onCastRaysClicked();
    void onRunSegmentationClicked();
    void onResetPointsClicked();
    
private:
    void setupUI();
    void computeDistanceTransform();
    void castRays();
    void findPeaksAlongRay(const cv::Vec2f& rayDir, const cv::Point& startPoint, const cv::Mat& distMap, const cv::Mat& sliceData);
    void runSegmentation();
    
    // UI elements
    QLabel* infoLabel;
    QDoubleSpinBox* angleStepSpinBox;
    QSpinBox* processesSpinBox;
    QSpinBox* thresholdSpinBox;  // Intensity threshold for peak detection
    QSpinBox* windowSizeSpinBox; // Window size for peak detection
    
    // Volume selection for segmentation
    QPushButton* selectVolumeButton;
    QLabel* volumePathLabel;
    QString selectedVolumePath;
    
    // Executable path
    QLineEdit* executablePathEdit;
    QString executablePath;
    
    QPushButton* castRaysButton;
    QPushButton* runSegmentationButton;
    QPushButton* resetPointsButton;
    QProgressBar* progressBar;
    
    // Data
    std::shared_ptr<volcart::VolumePkg> fVpkg;
    std::shared_ptr<volcart::Volume> currentVolume;
    cv::Vec3f selectedPoint;
    int currentZSlice;
    std::vector<cv::Vec3f> peakPoints;
    cv::Mat distanceTransform;
    bool hasSelectedPoint;
};

} // namespace ChaoVis
