#include "CDistanceTransformWidget.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QThread>
#include <QProcess>
#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcessEnvironment>

#include <opencv2/imgproc.hpp>

#include "vc/core/types/Volume.hpp"
#include "vc/core/types/VolumePkg.hpp"
#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Logging.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <functional>

namespace fs = std::filesystem;
namespace vc = volcart;

namespace ChaoVis {

CDistanceTransformWidget::CDistanceTransformWidget(QWidget* parent)
    : QWidget(parent)
    , fVpkg(nullptr)
    , currentVolume(nullptr)
    , chunkCache(nullptr)
    , currentZSlice(0)
    , hasSelectedPoint(false)
    , waitingForSeedPoint(false)
{
    setupUI();
    
    // Automatically find the executable path
    executablePath = findExecutablePath();
    if (executablePath.isEmpty()) {
        QMessageBox::warning(this, "Warning", 
            "Could not find vc_grow_seg_from_seed executable. "
            "Please ensure it is built and in your PATH or in the build directory.");
    }
}

CDistanceTransformWidget::~CDistanceTransformWidget() = default;

void CDistanceTransformWidget::setupUI()
{
    // Main layout
    auto mainLayout = new QVBoxLayout(this);
    
    // Info label
    infoLabel = new QLabel("Click 'Set Seed' to begin", this);
    mainLayout->addWidget(infoLabel);
    
    // Set seed button
    setSeedButton = new QPushButton("Set Seed", this);
    setSeedButton->setToolTip("Click to enable point selection mode");
    mainLayout->addWidget(setSeedButton);
    
    // Max radius control
    auto maxRadiusLayout = new QHBoxLayout();
    maxRadiusLayout->addWidget(new QLabel("Max Radius (pixels):", this));
    maxRadiusSpinBox = new QSpinBox(this);
    maxRadiusSpinBox->setRange(50, 20000);
    maxRadiusSpinBox->setValue(1500);
    maxRadiusSpinBox->setSingleStep(250);
    maxRadiusSpinBox->setToolTip("Maximum distance from center point for ray casting");
    maxRadiusLayout->addWidget(maxRadiusSpinBox);
    mainLayout->addLayout(maxRadiusLayout);
    
    // Angle step control
    auto angleStepLayout = new QHBoxLayout();
    angleStepLayout->addWidget(new QLabel("Angle Step (degrees):", this));
    angleStepSpinBox = new QDoubleSpinBox(this);
    angleStepSpinBox->setRange(1.0, 90.0);
    angleStepSpinBox->setValue(15.0);
    angleStepSpinBox->setSingleStep(1.0);
    angleStepLayout->addWidget(angleStepSpinBox);
    mainLayout->addLayout(angleStepLayout);
    
    // Processes control
    auto processesLayout = new QHBoxLayout();
    processesLayout->addWidget(new QLabel("Parallel Processes:", this));
    processesSpinBox = new QSpinBox(this);
    processesSpinBox->setRange(1, 16);
    processesSpinBox->setValue(4);
    processesLayout->addWidget(processesSpinBox);
    mainLayout->addLayout(processesLayout);
    
    // Intensity threshold control
    auto thresholdLayout = new QHBoxLayout();
    thresholdLayout->addWidget(new QLabel("Intensity Threshold:", this));
    thresholdSpinBox = new QSpinBox(this);
    thresholdSpinBox->setRange(1, 255);
    thresholdSpinBox->setValue(30);  // Default was hardcoded to 30
    thresholdSpinBox->setToolTip("Minimum intensity value for peak detection");
    thresholdLayout->addWidget(thresholdSpinBox);
    mainLayout->addLayout(thresholdLayout);
    
    // Window size control for peak detection
    auto windowSizeLayout = new QHBoxLayout();
    windowSizeLayout->addWidget(new QLabel("Peak Detection Window:", this));
    windowSizeSpinBox = new QSpinBox(this);
    windowSizeSpinBox->setRange(1, 10);
    windowSizeSpinBox->setValue(3);  // Default window size
    windowSizeSpinBox->setToolTip("Size of window for local maxima detection (larger values detect broader peaks)");
    windowSizeLayout->addWidget(windowSizeSpinBox);
    mainLayout->addLayout(windowSizeLayout);
    
    // Buttons
    castRaysButton = new QPushButton("Cast Rays", this);
    castRaysButton->setEnabled(false);
    mainLayout->addWidget(castRaysButton);
    
    runSegmentationButton = new QPushButton("Run Segmentation", this);
    runSegmentationButton->setEnabled(false);
    mainLayout->addWidget(runSegmentationButton);
    
    resetPointsButton = new QPushButton("Reset Points", this);
    resetPointsButton->setEnabled(false);
    mainLayout->addWidget(resetPointsButton);
    
    // Progress bar
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setVisible(false);
    mainLayout->addWidget(progressBar);
    
    // Connect signals
    connect(setSeedButton, &QPushButton::clicked, this, &CDistanceTransformWidget::onSetSeedClicked);
    connect(castRaysButton, &QPushButton::clicked, this, &CDistanceTransformWidget::onCastRaysClicked);
    connect(runSegmentationButton, &QPushButton::clicked, this, &CDistanceTransformWidget::onRunSegmentationClicked);
    connect(resetPointsButton, &QPushButton::clicked, this, &CDistanceTransformWidget::onResetPointsClicked);
    
    // Connect parameter changes to preview update
    connect(maxRadiusSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &CDistanceTransformWidget::updateParameterPreview);
    connect(angleStepSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            this, &CDistanceTransformWidget::updateParameterPreview);
    
    // Set size policy
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
}

void CDistanceTransformWidget::setVolumePkg(std::shared_ptr<volcart::VolumePkg> vpkg)
{
    fVpkg = vpkg;
}

void CDistanceTransformWidget::setCurrentVolume(std::shared_ptr<volcart::Volume> volume)
{
    currentVolume = volume;
    if (currentVolume) {
        castRaysButton->setEnabled(hasSelectedPoint);
    } else {
        castRaysButton->setEnabled(false);
        runSegmentationButton->setEnabled(false);
    }
}

void CDistanceTransformWidget::setCache(ChunkCache* cache)
{
    chunkCache = cache;
}

void CDistanceTransformWidget::onVolumeChanged(std::shared_ptr<volcart::Volume> vol)
{
    setCurrentVolume(vol);
}

void CDistanceTransformWidget::updateCurrentZSlice(int z)
{
    currentZSlice = z;
    // If we have a selected point, update its Z coordinate
    if (hasSelectedPoint) {
        selectedPoint[2] = z;
        infoLabel->setText(QString("Selected point: (%1, %2, %3)")
                               .arg(selectedPoint[0])
                               .arg(selectedPoint[1])
                               .arg(selectedPoint[2]));
    }
}

void CDistanceTransformWidget::onPointSelected(cv::Vec3f point, cv::Vec3f normal)
{
    // Only accept points when waiting for seed selection
    if (!waitingForSeedPoint) {
        return;
    }
    
    selectedPoint = point;
    currentZSlice = static_cast<int>(point[2]);
    hasSelectedPoint = true;
    waitingForSeedPoint = false;
    
    // Reset button text
    setSeedButton->setText("Set Seed");
    setSeedButton->setEnabled(true);
    
    infoLabel->setText(QString("Selected point: (%1, %2, %3)")
                           .arg(selectedPoint[0])
                           .arg(selectedPoint[1])
                           .arg(selectedPoint[2]));
    
    castRaysButton->setEnabled(currentVolume != nullptr);
    resetPointsButton->setEnabled(true);
    
    // Immediately show the parameter preview
    updateParameterPreview();
}

void CDistanceTransformWidget::onCastRaysClicked()
{
    if (!currentVolume || !hasSelectedPoint) {
        return;
    }
    
    // Reset previous peaks
    peakPoints.clear();
    emit sendPointsChanged({}, {});
    
    // Compute distance transform for the current slice
    computeDistanceTransform();
    
    // Cast rays and find peaks
    castRays();
    
    // Enable segmentation button if we found peaks
    runSegmentationButton->setEnabled(!peakPoints.empty());
    
    // Send points to display
    emit sendPointsChanged(peakPoints, {});
    
    // Update UI with clearer instructions about the displayed points
    infoLabel->setText(QString("Found %1 peaks (shown in red). Review points then click 'Run Segmentation'.").arg(peakPoints.size()));
    emit sendStatusMessageAvailable(
        QString("Cast %1 rays and found %2 intensity peaks. Points are displayed for review.").arg(360.0 / angleStepSpinBox->value()).arg(peakPoints.size()), 
        5000);
}

void CDistanceTransformWidget::computeDistanceTransform()
{
    if (!currentVolume) {
        return;
    }
    
    // Get the current slice data
    const int width = currentVolume->sliceWidth();
    const int height = currentVolume->sliceHeight();
    
    cv::Mat_<uint8_t> sliceData(height, width);
    
    // Extract the slice data from the volume
    cv::Mat_<cv::Vec3f> coords(height, width);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            coords(y, x) = cv::Vec3f(x, y, currentZSlice);
        }
    }
    
    // Read the slice data using the volume's dataset
    readInterpolated3D(sliceData, currentVolume->zarrDataset(0), coords, chunkCache);
    
    // Threshold the slice to create a binary image for distance transform
    cv::Mat binaryImage;
    cv::threshold(sliceData, binaryImage, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    
    // Compute the distance transform
    cv::distanceTransform(binaryImage, distanceTransform, cv::DIST_L2, cv::DIST_MASK_PRECISE);
    
    // Normalize the distance transform for visualization if needed
    cv::Mat distNormalized;
    cv::normalize(distanceTransform, distNormalized, 0, 255, cv::NORM_MINMAX);
    distNormalized.convertTo(distNormalized, CV_8UC1);
}

void CDistanceTransformWidget::castRays()
{
    if (distanceTransform.empty() || !currentVolume) {
        return;
    }
    
    // Get current slice data for intensity analysis
    const int width = currentVolume->sliceWidth();
    const int height = currentVolume->sliceHeight();
    
    cv::Mat_<uint8_t> sliceData(height, width);
    cv::Mat_<cv::Vec3f> coords(height, width);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            coords(y, x) = cv::Vec3f(x, y, currentZSlice);
        }
    }
    
    // Read the slice data using the volume's dataset
    readInterpolated3D(sliceData, currentVolume->zarrDataset(0), coords, chunkCache);
    
    // Setup progress tracking
    progressBar->setVisible(true);
    progressBar->setValue(0);
    
    // Cast rays at regular angle steps
    const double angleStep = angleStepSpinBox->value();
    const int numSteps = static_cast<int>(360.0 / angleStep);
    const cv::Point startPoint(selectedPoint[0], selectedPoint[1]);
    
    for (int i = 0; i < numSteps; i++) {
        // Calculate ray direction
        const double angle = i * angleStep * M_PI / 180.0;
        const cv::Vec2f rayDir(cos(angle), sin(angle));
        
        // Find peaks along this ray
        findPeaksAlongRay(rayDir, startPoint, distanceTransform, sliceData);
        
        // Update progress
        progressBar->setValue((i + 1) * 100 / numSteps);
        QApplication::processEvents();
    }
    
    progressBar->setVisible(false);
}

void CDistanceTransformWidget::findPeaksAlongRay(
    const cv::Vec2f& rayDir, 
    const cv::Point& startPoint, 
    const cv::Mat& distMap, 
    const cv::Mat& sliceData)
{
    const int maxRadius = maxRadiusSpinBox->value();
    const int width = sliceData.cols;
    const int height = sliceData.rows;
    
    std::vector<float> intensities;
    std::vector<cv::Point> positions;
    std::vector<float> distValues; // To store distance transform values
    
    // Get the window size from the spinbox
    const int window = windowSizeSpinBox->value();
    
    // Trace ray up to max radius
    for (int dist = 1; dist < maxRadius; dist++) {
        int x = startPoint.x + dist * rayDir[0];
        int y = startPoint.y + dist * rayDir[1];
        
        // Check bounds
        if (x < 0 || x >= width || y < 0 || y >= height) {
            break;
        }
        
        // Store intensity and position
        intensities.push_back(sliceData.at<uchar>(y, x));
        positions.push_back(cv::Point(x, y));
        
        // Store distance transform value if available
        if (!distMap.empty() && y < distMap.rows && x < distMap.cols) {
            distValues.push_back(distMap.at<float>(y, x));
        } else {
            distValues.push_back(0);
        }
    }
    
    if (intensities.empty()) {
        return;
    }
    
    // Enhanced local maxima detection with configurable window
    for (size_t i = window; i < intensities.size() - window; i++) {
        bool isLocalMax = true;
        
        // Check if this point is a local maximum within the window
        for (int j = -window; j <= window; j++) {
            if (j == 0) continue; // Skip comparing with self
            
            if (i + j >= 0 && i + j < intensities.size() && 
                intensities[i] <= intensities[i + j]) {
                isLocalMax = false;
                break;
            }
        }
        
        if (isLocalMax) {
            // Apply threshold
            if (intensities[i] > thresholdSpinBox->value()) {
                const cv::Point& pos = positions[i];
                peakPoints.push_back(cv::Vec3f(pos.x, pos.y, currentZSlice));
            }
        }
    }
    
    // Also check for sharp gradient changes (edge detection)
    for (size_t i = window; i < intensities.size() - window; i++) {
        // Skip if we're too close to already detected peaks
        bool tooClose = false;
        for (const auto& existingPoint : peakPoints) {
            if (std::abs(existingPoint[0] - positions[i].x) < window*2 && 
                std::abs(existingPoint[1] - positions[i].y) < window*2) {
                tooClose = true;
                break;
            }
        }
        
        if (tooClose) continue;
        
        // Check for significant gradient changes
        float leftAvg = 0, rightAvg = 0;
        for (int j = 1; j <= window; j++) {
            if (i - j >= 0) leftAvg += intensities[i - j];
            if (i + j < intensities.size()) rightAvg += intensities[i + j];
        }
        leftAvg /= window;
        rightAvg /= window;
        
        // If there's a significant gradient difference and the point is over threshold
        float gradientDiff = std::abs(leftAvg - rightAvg);
        if (gradientDiff > thresholdSpinBox->value() * 0.5 && 
            intensities[i] > thresholdSpinBox->value()) {
            
            // Check if it's already in the list of peaks (avoid duplicates)
            const cv::Point& pos = positions[i];
            peakPoints.push_back(cv::Vec3f(pos.x, pos.y, currentZSlice));
        }
    }
}

void CDistanceTransformWidget::onRunSegmentationClicked()
{
    if (peakPoints.empty() || !fVpkg) {
        QMessageBox::warning(this, "Error", "No points available for segmentation or volume package not loaded.");
        return;
    }
    
    // Update UI
    progressBar->setVisible(true);
    progressBar->setValue(0);
    infoLabel->setText("Running segmentation jobs...");
    runSegmentationButton->setEnabled(false);
    
    const int numProcesses = processesSpinBox->value();
    const int totalPoints = static_cast<int>(peakPoints.size());
    const int pointsPerBatch = std::max(1, totalPoints / numProcesses);
    
    // Use the existing segmentation directory structure
    // Check if there are existing segmentations first
    fs::path pathsDir;
    fs::path seedJsonPath;
    
    if (fVpkg->hasSegmentations()) {
        // If we have segmentations, get the path from one of them
        auto segID = fVpkg->segmentationIDs()[0];
        auto seg = fVpkg->segmentation(segID);
        pathsDir = seg->path().parent_path(); // This should be the "paths" directory
        seedJsonPath = pathsDir.parent_path() / "seed.json";
    } else {
        // Fallback: derive the path from the volume
        if (!fVpkg->hasVolumes()) {
            QMessageBox::warning(this, "Error", "No volumes in volume package.");
            progressBar->setVisible(false);
            runSegmentationButton->setEnabled(true);
            return;
        }
        
        auto vol = fVpkg->volume();
        // Volume is in "volumes/UUID", so we need to go up two levels to get to the volpkg root
        fs::path vpkgPath = vol->path().parent_path().parent_path();
        pathsDir = vpkgPath / "paths";
        seedJsonPath = vpkgPath / "seed.json";
        
        // Check if the paths directory exists
        if (!fs::exists(pathsDir)) {
            QMessageBox::warning(this, "Error", "Segmentation paths directory not found in volume package.");
            progressBar->setVisible(false);
            runSegmentationButton->setEnabled(true);
            return;
        }
    }
    
    // Check if seed.json exists
    if (!fs::exists(seedJsonPath)) {
        QMessageBox::warning(this, "Error", "seed.json not found in volume package.");
        progressBar->setVisible(false);
        runSegmentationButton->setEnabled(true);
        return;
    }
    
    // Get current volume ID
    QString volumeId;
    if (currentVolume) {
        for (const auto& id : fVpkg->volumeIDs()) {
            if (fVpkg->volume(id) == currentVolume) {
                volumeId = QString::fromStdString(id);
                break;
            }
        }
    }
    
    if (volumeId.isEmpty()) {
        QMessageBox::warning(this, "Error", "Could not determine current volume ID.");
        progressBar->setVisible(false);
        runSegmentationButton->setEnabled(true);
        return;
    }
    
    // Get the volume path from the current volume
    if (!currentVolume) {
        QMessageBox::warning(this, "Error", "No current volume selected.");
        progressBar->setVisible(false);
        runSegmentationButton->setEnabled(true);
        return;
    }
    
    // Use the current volume's path
    fs::path volumePath = currentVolume->path();
    
    auto segmentationTask = [this, volumeId, pathsDir, seedJsonPath, volumePath](const cv::Vec3f& point, int index) {
        // Create a unique name for this segmentation point (for logging only)
        QString segName = QString("dt_seg_%1_%2_%3_%4")
                             .arg(point[0])
                             .arg(point[1])
                             .arg(point[2])
                             .arg(index);
        
        // Use the current volume's path
        fs::path zarr_path = volumePath;
        
        // From source: vc_grow_seg_from_seed <ome-zarr-volume> <tgt-dir> <json-params> <seed-x> <seed-y> <seed-z>
        QString cmd = QString("%1 \"%2\" \"%3\" \"%4\" %5 %6 %7")
                         .arg(executablePath)
                         .arg(QString::fromStdString(zarr_path.string()))
                         .arg(QString::fromStdString(pathsDir.string()))
                         .arg(QString::fromStdString(seedJsonPath.string()))
                         .arg(point[0])
                         .arg(point[1])
                         .arg(point[2]);
        
        // Debug output to console to verify command
        std::cout << "Running seed segmentation: " << cmd.toStdString() << std::endl;
        
        // Execute the command from the volpkg root directory
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels); // Merge stdout and stderr
        process.setWorkingDirectory(QString::fromStdString(pathsDir.parent_path().string()));
        
        // Set explicit environment variables
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        process.setProcessEnvironment(env);
        
        // Try executing the command differently - using execute() instead of start()
        std::cout << "Executing command: " << cmd.toStdString() << std::endl;
        
        // Method 1: Use execute (synchronous, block until finished)
        int exitCode = QProcess::execute(cmd, QStringList());
        if (exitCode != 0) {
            std::cerr << "Execute method failed with exit code: " << exitCode << std::endl;
            
            // Method 2: Try again with system()
            std::cout << "Trying with system() call..." << std::endl;
            int sysResult = std::system(cmd.toStdString().c_str());
            if (sysResult != 0) {
                std::cerr << "System call also failed with code: " << sysResult << std::endl;
                
                // Method 3: Try with QProcess start()
                std::cout << "Trying with QProcess start()..." << std::endl;
                process.start(cmd);
                if (!process.waitForStarted(3000)) {
                    std::cerr << "Failed to start process: " << process.errorString().toStdString() << std::endl;
                } else {
                    process.waitForFinished(-1);
                    std::cout << "Process output: " << process.readAllStandardOutput().toStdString() << std::endl;
                    std::cerr << "Exit code: " << process.exitCode() << std::endl;
                }
            } else {
                std::cout << "System call succeeded!" << std::endl;
            }
        } else {
            std::cout << "Completed segmentation for point " << segName.toStdString() << std::endl;
        }
    };
    
    // Execute segmentation jobs in parallel, limited by the number of processes
    QList<QProcess*> processes;
    QList<int> activeIndices;
    int completedJobs = 0;
    
    // Process status update function
    auto updateProgress = [&]() {
        progressBar->setValue(completedJobs * 100 / totalPoints);
        QApplication::processEvents();
    };
    
    // Start as many initial processes as configured in the spinbox
    for (int i = 0; i < std::min(numProcesses, totalPoints); i++) {
        // Create process for this point
        QProcess* process = new QProcess(this);
        process->setProcessChannelMode(QProcess::MergedChannels);
        process->setWorkingDirectory(QString::fromStdString(pathsDir.parent_path().string()));
        
        // Prepare command
        const auto& point = peakPoints[i];
        QString cmd = QString("%1 \"%2\" \"%3\" \"%4\" %5 %6 %7")
                         .arg(executablePath)
                         .arg(QString::fromStdString(volumePath.string()))
                         .arg(QString::fromStdString(pathsDir.string()))
                         .arg(QString::fromStdString(seedJsonPath.string()))
                         .arg(point[0])
                         .arg(point[1])
                         .arg(point[2]);
        
        // Connect process finished signal
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [process, i, &processes, &activeIndices, &completedJobs, updateProgress, this](int exitCode, QProcess::ExitStatus exitStatus) {
                // Log completion
                if (exitCode != 0) {
                    std::cerr << "Process " << i << " failed with exit code: " << exitCode << std::endl;
                } else {
                    std::cout << "Completed segmentation for point " << i << std::endl;
                }
                
                // Remove from active list
                activeIndices.removeOne(i);
                processes.removeOne(process);
                process->deleteLater();
                
                // Update progress
                completedJobs++;
                updateProgress();
            });
        
        // Start the process with nice and ionice for better system behavior
        std::cout << "Starting job " << i << ": " << cmd.toStdString() << std::endl;
        process->start("nice", QStringList() << "-n" << "19" << "ionice" << "-c" << "3" << executablePath <<
                      QString::fromStdString(volumePath.string()) <<
                      QString::fromStdString(pathsDir.string()) <<
                      QString::fromStdString(seedJsonPath.string()) <<
                      QString::number(point[0]) <<
                      QString::number(point[1]) <<
                      QString::number(point[2]));
        
        processes.append(process);
        activeIndices.append(i);
    }
    
    // Keep track of next point to process
    int nextIndex = numProcesses;
    
    // Lambda to start next process
    std::function<void()> startNextProcess = [&]() {
        if (processes.size() < numProcesses && nextIndex < totalPoints) {
            // Create process for next point
            QProcess* process = new QProcess(this);
            process->setProcessChannelMode(QProcess::MergedChannels);
            process->setWorkingDirectory(QString::fromStdString(pathsDir.parent_path().string()));
            
            const auto& point = peakPoints[nextIndex];
            const int currentIndex = nextIndex;
            
            connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [process, currentIndex, &processes, &activeIndices, &completedJobs, updateProgress, &startNextProcess, this](int exitCode, QProcess::ExitStatus exitStatus) {
                    // Log completion
                    if (exitCode != 0) {
                        std::cerr << "Process " << currentIndex << " failed with exit code: " << exitCode << std::endl;
                    } else {
                        std::cout << "Completed segmentation for point " << currentIndex << std::endl;
                    }
                    
                    activeIndices.removeOne(currentIndex);
                    processes.removeOne(process);
                    process->deleteLater();
                    
                    completedJobs++;
                    updateProgress();
                    
                    startNextProcess();
                });
            
            std::cout << "Starting job " << currentIndex << std::endl;
            process->start("nice", QStringList() << "-n" << "19" << "ionice" << "-c" << "3" << executablePath <<
                          QString::fromStdString(volumePath.string()) <<
                          QString::fromStdString(pathsDir.string()) <<
                          QString::fromStdString(seedJsonPath.string()) <<
                          QString::number(point[0]) <<
                          QString::number(point[1]) <<
                          QString::number(point[2]));
            
            processes.append(process);
            activeIndices.append(currentIndex);
            nextIndex++;
        }
    };
    
    // Main event loop to handle process completion
    while (!processes.isEmpty() || completedJobs < totalPoints) {
        QApplication::processEvents(QEventLoop::AllEvents, 100);
        
        // Start new processes as slots become available
        while (processes.size() < numProcesses && nextIndex < totalPoints) {
            startNextProcess();
        }
    }
    
    // Final progress update
    progressBar->setValue(100);
    
    // Update UI
    progressBar->setVisible(false);
    infoLabel->setText(QString("Segmentation complete for %1 points.").arg(peakPoints.size()));
    runSegmentationButton->setEnabled(true);
    
    emit sendStatusMessageAvailable(
        QString("Completed segmentation for %1 points").arg(peakPoints.size()), 
        5000);
}

void CDistanceTransformWidget::onSetSeedClicked()
{
    waitingForSeedPoint = true;
    setSeedButton->setText("Click on volume to place seed...");
    setSeedButton->setEnabled(false);
    infoLabel->setText("Click on the volume viewer to select a seed point");
    emit sendStatusMessageAvailable("Click on the volume to place a seed point", 5000);
}

void CDistanceTransformWidget::onResetPointsClicked()
{
    peakPoints.clear();
    hasSelectedPoint = false;
    waitingForSeedPoint = false;
    
    setSeedButton->setText("Set Seed");
    setSeedButton->setEnabled(true);
    infoLabel->setText("Click 'Set Seed' to begin");
    
    castRaysButton->setEnabled(false);
    runSegmentationButton->setEnabled(false);
    resetPointsButton->setEnabled(false);
    
    emit sendPointsChanged({}, {});
}

QString CDistanceTransformWidget::findExecutablePath()
{
    // vc_grow_seg_from_seed should be in the same directory as the VC3D application
    QString execPath = QCoreApplication::applicationDirPath() + "/vc_grow_seg_from_seed";
    
    QFileInfo fileInfo(execPath);
    if (fileInfo.exists() && fileInfo.isExecutable()) {
        return fileInfo.absoluteFilePath();
    }
    
    // If not found, return empty string
    return QString();
}

void CDistanceTransformWidget::updateParameterPreview()
{
    if (!hasSelectedPoint || !currentVolume) {
        return;
    }
    
    // Clear any existing preview points
    std::vector<cv::Vec3f> previewPoints;
    std::vector<cv::Vec3f> seedPoints;
    
    // Add the seed point (will be displayed in blue)
    seedPoints.push_back(selectedPoint);
    
    // Get parameter values
    const double angleStep = angleStepSpinBox->value();
    const int maxRadius = maxRadiusSpinBox->value();
    const int numRays = static_cast<int>(360.0 / angleStep);
    
    // Generate preview points to show the radius and ray directions
    for (int i = 0; i < numRays; i++) {
        const double angle = i * angleStep * M_PI / 180.0;
        const cv::Vec2f rayDir(cos(angle), sin(angle));
        
        // Add points along the radius circle
        float x = selectedPoint[0] + maxRadius * rayDir[0];
        float y = selectedPoint[1] + maxRadius * rayDir[1];
        
        // Check bounds
        if (x >= 0 && x < currentVolume->sliceWidth() && 
            y >= 0 && y < currentVolume->sliceHeight()) {
            previewPoints.push_back(cv::Vec3f(x, y, currentZSlice));
        }
        
        // Add a few intermediate points along each ray for visualization
        for (int r = maxRadius / 4; r < maxRadius; r += maxRadius / 4) {
            x = selectedPoint[0] + r * rayDir[0];
            y = selectedPoint[1] + r * rayDir[1];
            
            if (x >= 0 && x < currentVolume->sliceWidth() && 
                y >= 0 && y < currentVolume->sliceHeight()) {
                previewPoints.push_back(cv::Vec3f(x, y, currentZSlice));
            }
        }
    }
    
    // Send the preview points to the volume viewer
    // Red points show the preview, blue point shows the seed
    emit sendPointsChanged(previewPoints, seedPoints);
    
    // Update the info label
    infoLabel->setText(QString("Seed at (%1, %2, %3) | Preview: %4 rays, radius %5px")
                           .arg(selectedPoint[0])
                           .arg(selectedPoint[1])
                           .arg(selectedPoint[2])
                           .arg(numRays)
                           .arg(maxRadius));
}

} // namespace ChaoVis
