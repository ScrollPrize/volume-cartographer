#include "CWindow.hpp"
#include "CSurfaceCollection.hpp"

#include <QSettings>
#include <QMessageBox>

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
    
    QString outputFormat = settings.value("rendering/output_path_format", "%s/layers/%02d.tif").toString();
    float scale = settings.value("rendering/scale", 1.0f).toFloat();
    int resolution = settings.value("rendering/resolution", 0).toInt();
    int layers = settings.value("rendering/layers", 21).toInt();
    
    QString segmentPath = QString::fromStdString(_vol_qsurfs[segmentId]->path.string());
    QString segmentOutDir = QString::fromStdString(_vol_qsurfs[segmentId]->path.string());
    QString outputPattern = outputFormat.replace("%s", segmentOutDir);
    
    // Initialize command line tool runner if needed
    if (!_cmdRunner) {
        _cmdRunner = new CommandLineToolRunner(statusBar(), this, this);
        connect(_cmdRunner, &CommandLineToolRunner::toolStarted, 
                [this](CommandLineToolRunner::Tool tool, const QString& message) {
                    statusBar()->showMessage(message, 0);
                });
        connect(_cmdRunner, &CommandLineToolRunner::toolFinished,
                [this](CommandLineToolRunner::Tool tool, bool success, const QString& message,
                       const QString& outputPath, bool copyToClipboard) {
                    if (success) {
                        QString displayMsg = message;
                        if (copyToClipboard) {
                            displayMsg += tr(" - Path copied to clipboard");
                        }
                        statusBar()->showMessage(displayMsg, 5000);
                        QMessageBox::information(this, tr("Rendering Complete"), displayMsg);
                    } else {
                        statusBar()->showMessage(tr("Rendering failed"), 5000);
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
    _cmdRunner->setSegmentPath(segmentPath);
    _cmdRunner->setOutputPattern(outputPattern);
    _cmdRunner->setRenderParams(scale, resolution, layers);
    
    _cmdRunner->execute(CommandLineToolRunner::Tool::RenderTifXYZ);
    
    statusBar()->showMessage(tr("Rendering segment: %1").arg(QString::fromStdString(segmentId)), 5000);
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
    QString srcSegment = QString::fromStdString(_vol_qsurfs[segmentId]->path.string());
    
    // Get the volpkg path and create traces directory if it doesn't exist
    fs::path volpkgPath = fs::path(fVpkgPath.toStdString());
    fs::path tracesDir = volpkgPath / "traces";
    fs::path jsonParamsPath = volpkgPath / "trace_params.json";
    fs::path pathsDir = volpkgPath / "paths";
    
    statusBar()->showMessage(tr("Preparing to run grow_seg_from_segment..."), 2000);
    
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
        QString(),  // Volume path will be set automatically in execute()
        QString::fromStdString(pathsDir.string()),
        QString::fromStdString(tracesDir.string()),
        QString::fromStdString(jsonParamsPath.string()),
        srcSegment
    );
    
    // Show console before executing to see any debug output
    _cmdRunner->showConsoleOutput();
    
    _cmdRunner->execute(CommandLineToolRunner::Tool::GrowSegFromSegment);
    
    statusBar()->showMessage(tr("Growing segment from: %1").arg(QString::fromStdString(segmentId)), 5000);
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
    
    statusBar()->showMessage(tr("Adding overlap for segment: %1").arg(QString::fromStdString(segmentId)), 5000);
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
    
    // Get source tifxyz path (this is a directory containing the TIFXYZ files)
    fs::path tifxyzPath = _vol_qsurfs[segmentId]->path;
    
    // Generate output OBJ path inside the TIFXYZ directory with segment ID as filename
    fs::path objPath = tifxyzPath / (segmentId + ".obj");
    
    // Set up parameters and execute the tool
    _cmdRunner->setToObjParams(
        QString::fromStdString(tifxyzPath.string()),
        QString::fromStdString(objPath.string())
    );
    
    _cmdRunner->execute(CommandLineToolRunner::Tool::tifxyz2obj);
    
    statusBar()->showMessage(tr("Converting segment to OBJ: %1").arg(QString::fromStdString(segmentId)), 5000);
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
    fs::path volpkgPath = fs::path(fVpkgPath.toStdString());
    fs::path pathsDir = volpkgPath / "paths";
    
    // Create traces directory if it doesn't exist
    if (!fs::exists(pathsDir)) {
        QMessageBox::warning(this, tr("Error"), tr("Paths directory not found in the volpkg"));
        return;
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
        QString(),  // Volume path will be set automatically in execute()
        QString::fromStdString(pathsDir.string()),
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
    statusBar()->showMessage(tr("Growing segment using %1 in %2").arg(jsonFileName).arg(modeDesc), 5000);
}

// Helper method to initialize command line runner
bool CWindow::initializeCommandLineRunner()
{
    if (!_cmdRunner) {
        _cmdRunner = new CommandLineToolRunner(statusBar(), this, this);
        
        // Read parallel processes and iteration count settings from INI file
        QSettings settings("VC.ini", QSettings::IniFormat);
        int parallelProcesses = settings.value("perf/parallel_processes", 8).toInt();
        int iterationCount = settings.value("perf/iteration_count", 1000).toInt();
        
        // Apply the settings
        _cmdRunner->setParallelProcesses(parallelProcesses);
        _cmdRunner->setIterationCount(iterationCount);
        
        connect(_cmdRunner, &CommandLineToolRunner::toolStarted, 
                [this](CommandLineToolRunner::Tool tool, const QString& message) {
                    statusBar()->showMessage(message, 0);
                });
        connect(_cmdRunner, &CommandLineToolRunner::toolFinished, 
                [this](CommandLineToolRunner::Tool tool, bool success, const QString& message, 
                       const QString& outputPath, bool copyToClipboard) {
                    if (success) {
                        QString displayMsg = message;
                        if (copyToClipboard) {
                            displayMsg += tr(" - Path copied to clipboard");
                        }
                        statusBar()->showMessage(displayMsg, 5000);
                        QMessageBox::information(this, tr("Operation Complete"), displayMsg);
                    } else {
                        statusBar()->showMessage(tr("Operation failed"), 5000);
                        QMessageBox::critical(this, tr("Error"), message);
                    }
                });
    }
    return true;
}

void CWindow::onDeleteSegments(const std::vector<SurfaceID>& segmentIds)
{
    if (segmentIds.empty()) {
        return;
    }
    
    // Create confirmation message
    QString message;
    if (segmentIds.size() == 1) {
        message = tr("Are you sure you want to delete segment '%1'?\n\nThis action cannot be undone.")
                    .arg(QString::fromStdString(segmentIds[0]));
    } else {
        message = tr("Are you sure you want to delete %1 segments?\n\nThis action cannot be undone.")
                    .arg(segmentIds.size());
    }
    
    // Show confirmation dialog
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("Confirm Deletion"), message,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    // Delete each segment
    int successCount = 0;
    QStringList failedSegments;
    bool needsReload = false;
    
    for (const auto& segmentId : segmentIds) {
        try {
            // Use the VolumePkg's removeSegmentation method
            fVpkg->removeSegmentation(segmentId);
            successCount++;
            needsReload = true;
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Failed to delete segment " << segmentId << ": " << e.what() << std::endl;
            
            // Check if it's a permission error
            if (e.code() == std::errc::permission_denied) {
                failedSegments << QString::fromStdString(segmentId) + " (permission denied)";
            } else {
                failedSegments << QString::fromStdString(segmentId) + " (filesystem error)";
            }
        } catch (const std::exception& e) {
            failedSegments << QString::fromStdString(segmentId);
            std::cerr << "Failed to delete segment " << segmentId << ": " << e.what() << std::endl;
        }
    }
    
    // Only update UI if we successfully deleted something
    if (needsReload) {
        try {
            // Use incremental removal to update the UI for each successfully deleted segment
            for (const auto& segmentId : segmentIds) {
                // Only remove from UI if it was successfully deleted from disk
                if (std::find(failedSegments.begin(), failedSegments.end(), 
                            QString::fromStdString(segmentId)) == failedSegments.end() &&
                    std::find(failedSegments.begin(), failedSegments.end(), 
                            QString::fromStdString(segmentId) + " (permission denied)") == failedSegments.end() &&
                    std::find(failedSegments.begin(), failedSegments.end(), 
                            QString::fromStdString(segmentId) + " (filesystem error)") == failedSegments.end()) {
                    RemoveSingleSegmentation(segmentId);
                }
            }
            
            // Update the volpkg label and filters
            UpdateVolpkgLabel(0);
            onSegFilterChanged(0);
        } catch (const std::exception& e) {
            std::cerr << "Error updating UI after deletion: " << e.what() << std::endl;
            QMessageBox::warning(this, tr("Warning"), 
                               tr("Segments were deleted but there was an error refreshing the list. "
                                  "Please reload surfaces manually."));
        }
    }
    
    // Show result message
    if (successCount == segmentIds.size()) {
        statusBar()->showMessage(tr("Successfully deleted %1 segment(s)").arg(successCount), 5000);
    } else if (successCount > 0) {
        QMessageBox::warning(this, tr("Partial Success"),
            tr("Deleted %1 segment(s), but failed to delete: %2\n\n"
               "Note: Permission errors may require manual deletion or running with elevated privileges.")
            .arg(successCount)
            .arg(failedSegments.join(", ")));
    } else {
        QMessageBox::critical(this, tr("Deletion Failed"),
            tr("Failed to delete any segments.\n\n"
               "Failed segments: %1\n\n"
               "This may be due to insufficient permissions. "
               "Try running the application with elevated privileges or manually delete the folders.")
            .arg(failedSegments.join(", ")));
    }
}
