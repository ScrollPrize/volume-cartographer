#include "CWindow.hpp"
#include "CSurfaceCollection.hpp"

#include <QSettings>

#include "vc/core/types/VolumePkg.hpp"
#include "vc/core/util/Surface.hpp"

namespace vc = volcart;
using namespace ChaoVis;
namespace fs = std::filesystem;

void CWindow::onRenderSegment(const SurfaceID& segmentId)
{
    if (currentVolume == nullptr || !_vol_qsurfs.count(segmentId)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot render segment: No volume or invalid segment selected"));
        return;
    }
    
    QSettings settings("VC.ini", QSettings::IniFormat);
    
    QString defaultVolume = settings.value("rendering/default_volume", "").toString();
    QString outputFormat = settings.value("rendering/output_path_format", "%s/layers/%02d.tif").toString();
    float scale = settings.value("rendering/scale", 1.0f).toFloat();
    int resolution = settings.value("rendering/resolution", 0).toInt();
    int layers = settings.value("rendering/layers", 21).toInt();
    
    std::shared_ptr<volcart::Volume> volumeToRender;
    if (defaultVolume.isEmpty()) {
        volumeToRender = currentVolume;
    } else {
        try {
            volumeToRender = fVpkg->volume(defaultVolume.toStdString());
        } catch (const std::exception& e) {
            QMessageBox::warning(this, tr("Error"), tr("Default volume not found. Using current volume instead."));
            volumeToRender = currentVolume;
        }
    }
    
    QString volumePath = QString::fromStdString(volumeToRender->path().string());
    QString segmentPath = QString::fromStdString(_vol_qsurfs[segmentId]->path.string());
    QString segmentOutDir = QString::fromStdString(_vol_qsurfs[segmentId]->path.string());
    QString outputPattern = outputFormat.replace("%s", segmentOutDir);
    
    // Initialize command line tool runner if needed
    if (!_cmdRunner) {
        _cmdRunner = new CommandLineToolRunner(statusBar, this);
        connect(_cmdRunner, &CommandLineToolRunner::toolStarted, 
                [this](CommandLineToolRunner::Tool tool, const QString& message) {
                    statusBar->showMessage(message, 0);
                });
        connect(_cmdRunner, &CommandLineToolRunner::toolFinished, 
                [this](CommandLineToolRunner::Tool tool, bool success, const QString& message, 
                       const QString& outputPath, bool copyToClipboard) {
                    if (success) {
                        QString displayMsg = message;
                        if (copyToClipboard) {
                            displayMsg += tr(" - Path copied to clipboard");
                        }
                        statusBar->showMessage(displayMsg, 5000);
                        QMessageBox::information(this, tr("Rendering Complete"), displayMsg);
                    } else {
                        statusBar->showMessage(tr("Rendering failed"), 5000);
                        QMessageBox::critical(this, tr("Rendering Error"), message);
                    }
                });
    }
    
    // Check if a tool is already running
    if (_cmdRunner->isRunning()) {
        QMessageBox::warning(this, tr("Warning"), tr("A command line tool is already running."));
        return;
    }
    
    // Set up parameters and execute the render tool
    _cmdRunner->setVolumePath(volumePath);
    _cmdRunner->setSegmentPath(segmentPath);
    _cmdRunner->setOutputPattern(outputPattern);
    _cmdRunner->setRenderParams(scale, resolution, layers);
    
    _cmdRunner->execute(CommandLineToolRunner::Tool::RenderTifXYZ);
    
    statusBar->showMessage(tr("Rendering segment: %1").arg(QString::fromStdString(segmentId)), 5000);
}

void CWindow::onGrowSegmentFromSegment(const SurfaceID& segmentId)
{
    if (currentVolume == nullptr || !_vol_qsurfs.count(segmentId)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot grow segment: No volume or invalid segment selected"));
        return;
    }
    
    // Initialize command line tool runner if needed
    if (!initializeCommandLineRunner()) {
        return;
    }
    
    // Check if a tool is already running
    if (_cmdRunner->isRunning()) {
        QMessageBox::warning(this, tr("Warning"), tr("A command line tool is already running."));
        return;
    }
    
    // Get paths
    QString volumePath = getCurrentVolumePath();
    if (volumePath.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot grow segment: No volume selected"));
        return;
    }
    QString srcSegment = QString::fromStdString(_vol_qsurfs[segmentId]->path.string());
    
    // Get the volpkg path and create traces directory if it doesn't exist
    fs::path volpkgPath = fs::path(fVpkgPath.toStdString());
    fs::path tracesDir = volpkgPath / "traces";
    fs::path jsonParamsPath = volpkgPath / "trace_params.json";
    fs::path pathsDir = volpkgPath / "paths";
    
    // Log information for debugging - write to console via statusBar
    QString debugInfo = tr("Volume path: %1\n").arg(volumePath);
    debugInfo += tr("Source segment: %1\n").arg(srcSegment);
    debugInfo += tr("Source directory: %1\n").arg(QString::fromStdString(pathsDir.string()));
    debugInfo += tr("Target directory: %1\n").arg(QString::fromStdString(tracesDir.string()));
    debugInfo += tr("JSON parameters: %1").arg(QString::fromStdString(jsonParamsPath.string()));
    statusBar->showMessage(tr("Preparing to run grow_seg_from_segment..."), 2000);
    
    // Create traces directory if it doesn't exist
    if (!fs::exists(tracesDir)) {
        try {
            fs::create_directory(tracesDir);
        } catch (const std::exception& e) {
            QMessageBox::warning(this, tr("Error"), tr("Failed to create traces directory: %1").arg(e.what()));
            return;
        }
    }
    
    // Check if trace_params.json exists
    if (!fs::exists(jsonParamsPath)) {
        QMessageBox::warning(this, tr("Error"), tr("trace_params.json not found in the volpkg"));
        return;
    }
    
    // Set up parameters and execute the tool
    _cmdRunner->setTraceParams(
        volumePath,
        QString::fromStdString(pathsDir.string()),
        QString::fromStdString(tracesDir.string()),
        QString::fromStdString(jsonParamsPath.string()),
        srcSegment
    );
    
    // Show console before executing to see any debug output
    _cmdRunner->showConsoleOutput();
    
    _cmdRunner->execute(CommandLineToolRunner::Tool::GrowSegFromSegment);
    
    statusBar->showMessage(tr("Growing segment from: %1").arg(QString::fromStdString(segmentId)), 5000);
}

void CWindow::onAddOverlap(const SurfaceID& segmentId)
{
    if (currentVolume == nullptr || !_vol_qsurfs.count(segmentId)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot add overlap: No volume or invalid segment selected"));
        return;
    }
    
    // Initialize command line tool runner if needed
    if (!initializeCommandLineRunner()) {
        return;
    }
    
    // Check if a tool is already running
    if (_cmdRunner->isRunning()) {
        QMessageBox::warning(this, tr("Warning"), tr("A command line tool is already running."));
        return;
    }
    
    // Get paths
    fs::path volpkgPath = fs::path(fVpkgPath.toStdString());
    fs::path pathsDir = volpkgPath / "paths";
    QString tifxyzPath = QString::fromStdString(_vol_qsurfs[segmentId]->path.string());
    
    // Set up parameters and execute the tool
    _cmdRunner->setAddOverlapParams(
        QString::fromStdString(pathsDir.string()),
        tifxyzPath
    );
    
    _cmdRunner->execute(CommandLineToolRunner::Tool::SegAddOverlap);
    
    statusBar->showMessage(tr("Adding overlap for segment: %1").arg(QString::fromStdString(segmentId)), 5000);
}

void CWindow::onConvertToObj(const SurfaceID& segmentId)
{
    if (currentVolume == nullptr || !_vol_qsurfs.count(segmentId)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot convert to OBJ: No volume or invalid segment selected"));
        return;
    }
    
    // Initialize command line tool runner if needed
    if (!initializeCommandLineRunner()) {
        return;
    }
    
    // Check if a tool is already running
    if (_cmdRunner->isRunning()) {
        QMessageBox::warning(this, tr("Warning"), tr("A command line tool is already running."));
        return;
    }
    
    // Get source tifxyz path
    fs::path tifxyzPath = _vol_qsurfs[segmentId]->path;
    
    // Generate output OBJ path (same directory with .obj extension)
    fs::path objPath = tifxyzPath;
    objPath.replace_extension(".obj");
    
    // Set up parameters and execute the tool
    _cmdRunner->setToObjParams(
        QString::fromStdString(tifxyzPath.string()),
        QString::fromStdString(objPath.string())
    );
    
    _cmdRunner->execute(CommandLineToolRunner::Tool::tifxyz2obj);
    
    statusBar->showMessage(tr("Converting segment to OBJ: %1").arg(QString::fromStdString(segmentId)), 5000);
}

void CWindow::onGrowSeeds(const SurfaceID& segmentId, bool isExpand, bool isRandomSeed)
{
    if (currentVolume == nullptr) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot grow seeds: No volume loaded"));
        return;
    }
    
    // Initialize command line tool runner if needed
    if (!initializeCommandLineRunner()) {
        return;
    }
    
    // Check if a tool is already running
    if (_cmdRunner->isRunning()) {
        QMessageBox::warning(this, tr("Warning"), tr("A command line tool is already running."));
        return;
    }
    
    // Get paths
    QString volumePath = getCurrentVolumePath();
    if (volumePath.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot grow seeds: No volume selected"));
        return;
    }
    fs::path volpkgPath = fs::path(fVpkgPath.toStdString());
    fs::path tracesDir = volpkgPath / "traces";
    
    // Create traces directory if it doesn't exist
    if (!fs::exists(tracesDir)) {
        try {
            fs::create_directory(tracesDir);
        } catch (const std::exception& e) {
            QMessageBox::warning(this, tr("Error"), tr("Failed to create traces directory: %1").arg(e.what()));
            return;
        }
    }
    
    // Get JSON parameters file
    QString jsonFileName = isExpand ? "expand.json" : "seed.json";
    fs::path jsonParamsPath = volpkgPath / jsonFileName.toStdString();
    
    // Check if JSON file exists
    if (!fs::exists(jsonParamsPath)) {
        QMessageBox::warning(this, tr("Error"), tr("%1 not found in the volpkg").arg(jsonFileName));
        return;
    }
    
    // Get current POI (focus point) for seed coordinates if needed
    int seedX = 0, seedY = 0, seedZ = 0;
    if (!isExpand && !isRandomSeed) {
        POI *poi = _surf_col->poi("focus");
        if (!poi) {
            QMessageBox::warning(this, tr("Error"), tr("No focus point selected. Click on a volume with Ctrl key to set a seed point."));
            return;
        }
        seedX = static_cast<int>(poi->p[0]);
        seedY = static_cast<int>(poi->p[1]);
        seedZ = static_cast<int>(poi->p[2]);
    }
    
    // Set up parameters and execute the tool
    _cmdRunner->setGrowParams(
        volumePath,
        QString::fromStdString(tracesDir.string()),
        QString::fromStdString(jsonParamsPath.string()),
        seedX,
        seedY,
        seedZ,
        isExpand,
        isRandomSeed
    );
    
    _cmdRunner->execute(CommandLineToolRunner::Tool::GrowSegFromSeeds);
    
    QString modeDesc = isExpand ? "expand mode" : 
                      (isRandomSeed ? "random seed mode" : "seed mode");
    statusBar->showMessage(tr("Growing segment using %1 in %2").arg(jsonFileName).arg(modeDesc), 5000);
}

// Helper method to initialize command line runner
bool CWindow::initializeCommandLineRunner()
{
    if (!_cmdRunner) {
        _cmdRunner = new CommandLineToolRunner(statusBar, this);
        
        // Read parallel processes and iteration count settings from INI file
        QSettings settings("VC.ini", QSettings::IniFormat);
        int parallelProcesses = settings.value("perf/parallel_processes", 8).toInt();
        int iterationCount = settings.value("perf/iteration_count", 1000).toInt();
        
        // Apply the settings
        _cmdRunner->setParallelProcesses(parallelProcesses);
        _cmdRunner->setIterationCount(iterationCount);
        
        connect(_cmdRunner, &CommandLineToolRunner::toolStarted, 
                [this](CommandLineToolRunner::Tool tool, const QString& message) {
                    statusBar->showMessage(message, 0);
                });
        connect(_cmdRunner, &CommandLineToolRunner::toolFinished, 
                [this](CommandLineToolRunner::Tool tool, bool success, const QString& message, 
                       const QString& outputPath, bool copyToClipboard) {
                    if (success) {
                        QString displayMsg = message;
                        if (copyToClipboard) {
                            displayMsg += tr(" - Path copied to clipboard");
                        }
                        statusBar->showMessage(displayMsg, 5000);
                        QMessageBox::information(this, tr("Operation Complete"), displayMsg);
                    } else {
                        statusBar->showMessage(tr("Operation failed"), 5000);
                        QMessageBox::critical(this, tr("Error"), message);
                    }
                });
    }
    return true;
}
