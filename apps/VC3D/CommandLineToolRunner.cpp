#include "CommandLineToolRunner.hpp"
#include "CWindow.hpp"
#include <QDir>
#include <QFileInfo>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QCoreApplication>

namespace ChaoVis {

CommandLineToolRunner::CommandLineToolRunner(QStatusBar* statusBar, CWindow* mainWindow, QObject* parent)
    : QObject(parent)
    , _mainWindow(mainWindow)
    , _progressUtil(new ProgressUtil(nullptr, this))
    , _process(nullptr)
    , _consoleOutput(new ConsoleOutputWidget())
    , _consoleDialog(new QDialog(nullptr, Qt::Window))
    , _autoShowConsole(true)
    , _scale(1.0f)
    , _resolution(0)
    , _layers(31)
    , _seed_x(0)
    , _seed_y(0)
    , _seed_z(0)
    , _parallelProcesses(8)
    , _iterationCount(1000)
{
    // set up the console output dialog
    _consoleDialog->setWindowTitle(tr("Command Output"));
    _consoleDialog->resize(700, 500);
    
    QVBoxLayout* layout = new QVBoxLayout(_consoleDialog);
    layout->addWidget(_consoleOutput);
    _consoleDialog->setLayout(layout);
}

CommandLineToolRunner::~CommandLineToolRunner()
{
    if (_process) {
        if (_process->state() != QProcess::NotRunning) {
            _process->terminate();
            _process->waitForFinished(3000);
        }
        delete _process;
    }
    
    delete _consoleDialog;
    // _consoleOutput is deleted by _consoleDialog
}

void CommandLineToolRunner::setVolumePath(const QString& path)
{
    _volumePath = path;
}

void CommandLineToolRunner::setSegmentPath(const QString& path)
{
    _segmentPath = path;
}

void CommandLineToolRunner::setOutputPattern(const QString& pattern)
{
    _outputPattern = pattern;
}

void CommandLineToolRunner::setRenderParams(float scale, int resolution, int layers)
{
    _scale = scale;
    _resolution = resolution;
    _layers = layers;
}

void CommandLineToolRunner::setGrowParams(QString volumePath, QString tgtDir, QString jsonParams, int seed_x, int seed_y, int seed_z, bool useExpandMode, bool useRandomSeed)
{
    _volumePath = volumePath;
    _tgtDir = tgtDir;
    _jsonParams = jsonParams;
    _seed_x = seed_x;
    _seed_y = seed_y;
    _seed_z = seed_z;
    _useExpandMode = useExpandMode;
    _useRandomSeed = useRandomSeed;
    
    // Update the JSON file with the correct mode parameter
    QFile file(_jsonParams);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray jsonData = file.readAll();
        file.close();
        
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        QJsonObject jsonObj = doc.object();
        
        // Set the mode based on the flags
        if (useExpandMode) {
            jsonObj["mode"] = "expansion";
        } else if (useRandomSeed) {
            jsonObj["mode"] = "random_seed";
        } else {
            jsonObj["mode"] = "explicit_seed";
        }
        
        // Write the updated JSON back to the file
        doc.setObject(jsonObj);
        jsonData = doc.toJson();
        
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(jsonData);
            file.close();
        }
    }
}

void CommandLineToolRunner::setTraceParams(QString volumePath, QString srcDir, QString tgtDir, QString jsonParams, QString srcSegment)
{
    _volumePath = volumePath;
    _srcDir = srcDir;
    _tgtDir = tgtDir;
    _jsonParams = jsonParams;
    _srcSegment = srcSegment;
}

void CommandLineToolRunner::setAddOverlapParams(QString tgtDir, QString tifxyzPath)
{
    _tgtDir = tgtDir;
    _tifxyzPath = tifxyzPath;
}

void CommandLineToolRunner::setToObjParams(QString tifxyzPath, QString objPath)
{
    _tifxyzPath = tifxyzPath;
    _objPath = objPath;
}

bool CommandLineToolRunner::execute(Tool tool)
{
    if (_process && _process->state() != QProcess::NotRunning) {
        QMessageBox::warning(nullptr, tr("Warning"), tr("A tool is already running."));
        return false;
    }
    
    // clear previous console output
    _consoleOutput->clear();
    
    // Verify tool executable exists
    QString toolCmd = toolName(tool);
    QFileInfo toolInfo(toolCmd);
    if (!toolInfo.exists() || !toolInfo.isExecutable()) {
        QString errorMsg = tr("Tool executable not found or not executable: %1").arg(toolCmd);
        _consoleOutput->appendOutput(errorMsg);
        showConsoleOutput();
        QMessageBox::warning(nullptr, tr("Error"), errorMsg);
        return false;
    }
    
    // Get the current volume path from the main window
    if (_mainWindow) {
        QString currentVolumePath = _mainWindow->getCurrentVolumePath();
        if (currentVolumePath.isEmpty()) {
            QMessageBox::warning(nullptr, tr("Error"), tr("No volume selected."));
            return false;
        }
        _volumePath = currentVolumePath;
    } else if (_volumePath.isEmpty()) {
        QMessageBox::warning(nullptr, tr("Error"), tr("Volume path not specified and no main window available."));
        return false;
    }
    
    if (tool == Tool::RenderTifXYZ && _segmentPath.isEmpty()) {
        QMessageBox::warning(nullptr, tr("Error"), tr("Segment path not specified."));
        return false;
    }
    
    if (tool == Tool::GrowSegFromSegment && _srcSegment.isEmpty()) {
        QMessageBox::warning(nullptr, tr("Error"), tr("Source segment not specified."));
        return false;
    }
    
    if (tool == Tool::RenderTifXYZ) {
        if (_outputPattern.isEmpty()) {
            QMessageBox::warning(nullptr, tr("Error"), tr("Output pattern not specified."));
            return false;
        }
        
        QFileInfo outputInfo(_outputPattern);
        QDir outputDir = outputInfo.dir();
        if (!outputDir.exists()) {
            if (!outputDir.mkpath(".")) {
                QMessageBox::warning(nullptr, tr("Error"), tr("Failed to create output directory: %1").arg(outputDir.path()));
                return false;
            }
        }
    }
    
    _currentTool = tool;
    
    if (!_process) {
        _process = new QProcess(this);
        _process->setProcessChannelMode(QProcess::MergedChannels);
        
        connect(_process, &QProcess::started, this, &CommandLineToolRunner::onProcessStarted);
        connect(_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
                this, &CommandLineToolRunner::onProcessFinished);
        connect(_process, &QProcess::errorOccurred, this, &CommandLineToolRunner::onProcessError);
        connect(_process, &QProcess::readyRead, this, &CommandLineToolRunner::onProcessReadyRead);
    }
    
    QStringList args = buildArguments(tool);
    QString toolCommand = toolName(tool);
    
    QString startMessage;
    
    // vc_grow_seg_from_seeds needs to use xargs for parallell processes
    if (tool == Tool::GrowSegFromSeeds) {
        startMessage = tr("Starting %1 with %2 parallel processes for: %3")
                          .arg(toolCommand)
                          .arg(_parallelProcesses)
                          .arg(QFileInfo(_segmentPath).fileName());
        
        QString baseCmd = QString("%1 %2").arg(toolCommand).arg(args.join(" "));
        QString shellCmd = QString("time seq 1 %1 | xargs -i -P %2 bash -c 'nice ionice %3 || true'")
                              .arg(_iterationCount)
                              .arg(_parallelProcesses)
                              .arg(baseCmd.replace("'", "\\'"));
                              
        emit toolStarted(_currentTool, startMessage);
        
        _consoleOutput->setTitle(tr("Running: %1 (with xargs)").arg(toolCommand));
        
        if (_autoShowConsole) {
            showConsoleOutput();
        }
        
        _process->setProgram("/bin/bash");
        _process->setArguments(QStringList() << "-c" << shellCmd);
        _process->start();
    } else {
        startMessage = tr("Starting %1 for: %2").arg(toolCommand).arg(QFileInfo(_segmentPath).fileName());
        emit toolStarted(_currentTool, startMessage);
        
        _consoleOutput->setTitle(tr("Running: %1").arg(toolCommand));
        
        if (_autoShowConsole) {
            showConsoleOutput();
        }
        
        _process->start(toolCommand, args);
    }
    
    return true;
}

void CommandLineToolRunner::cancel()
{
    if (_process && _process->state() != QProcess::NotRunning) {
        _process->terminate();
    }
}

bool CommandLineToolRunner::isRunning() const
{
    return (_process && _process->state() != QProcess::NotRunning);
}

void CommandLineToolRunner::showConsoleOutput()
{
    if (_consoleDialog) {
        _consoleDialog->show();
        _consoleDialog->raise();
        _consoleDialog->activateWindow();
    }
}

void CommandLineToolRunner::hideConsoleOutput()
{
    if (_consoleDialog) {
        _consoleDialog->hide();
    }
}

void CommandLineToolRunner::setAutoShowConsoleOutput(bool autoShow)
{
    _autoShowConsole = autoShow;
}

void CommandLineToolRunner::setParallelProcesses(int count)
{
    _parallelProcesses = count;
}

void CommandLineToolRunner::setIterationCount(int count)
{
    _iterationCount = count;
}

void CommandLineToolRunner::onProcessReadyRead()
{
    if (_process) {
        QByteArray output = _process->readAll();
        QString outputText = QString::fromUtf8(output);
        
        // append to console widget
        _consoleOutput->appendOutput(outputText);
        
        // emit signal for other components that might want the output
        emit consoleOutputReceived(outputText);
    }
}

void CommandLineToolRunner::onProcessStarted()
{
    QString message = tr("Running %1...").arg(toolName(_currentTool));
    if (_progressUtil) _progressUtil->startAnimation(message);
}

void CommandLineToolRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        QString message = tr("%1 completed successfully").arg(toolName(_currentTool));
        QString outputPath = getOutputPath();
        
        // the runner can copy the output of a process to the clipboard, currently this only makes sense for rendering
        // so the user can quickly open the output dir
        bool copyToClipboard = (_currentTool == Tool::RenderTifXYZ);
        
        if (copyToClipboard) {
            QApplication::clipboard()->setText(outputPath);
            if (_progressUtil) _progressUtil->stopAnimation(message + tr(" - Path copied to clipboard"));
        } else {
            if (_progressUtil) _progressUtil->stopAnimation(message);
        }
        
        emit toolFinished(_currentTool, true, message, outputPath, copyToClipboard);
    } else {
        QString errorMessage = tr("%1 failed with exit code: %2")
                                .arg(toolName(_currentTool))
                                .arg(exitCode);
        
        if (_progressUtil) _progressUtil->stopAnimation(tr("Process failed"));
        
        emit toolFinished(_currentTool, false, errorMessage, QString(), false);
    }
}

void CommandLineToolRunner::onProcessError(QProcess::ProcessError error)
{
    QString errorMessage = tr("Error running %1: ").arg(toolName(_currentTool));
    
    switch (error) {
        case QProcess::FailedToStart: 
            errorMessage += tr("failed to start - Tool executable not found or not executable. Command: %1").arg(toolName(_currentTool)); 
            break;
        case QProcess::Crashed: errorMessage += tr("crashed"); break;
        case QProcess::Timedout: errorMessage += tr("timed out"); break;
        case QProcess::WriteError: errorMessage += tr("write error"); break;
        case QProcess::ReadError: errorMessage += tr("read error"); break;
        default: errorMessage += tr("unknown error"); break;
    }
    
    // Add the arguments that were used for better debugging
    QStringList args = buildArguments(_currentTool);
    errorMessage += tr("\nArguments: %1").arg(args.join(" "));
    
    if (_progressUtil) _progressUtil->stopAnimation(tr("Process failed"));
    
    emit toolFinished(_currentTool, false, errorMessage, QString(), false);
    
    // Also print to console for easier debugging
    if (_consoleOutput) {
        _consoleOutput->appendOutput(errorMessage);
    }
    
    // Show the console output automatically on errors
    showConsoleOutput();
}

QStringList CommandLineToolRunner::buildArguments(Tool tool)
{
    QStringList args;
    
    switch (tool) {
        case Tool::RenderTifXYZ:
            args << "--volume" << _volumePath
                 << "--output" << _outputPattern
                 << "--segmentation" << _segmentPath
                 << "--scale" << QString::number(_scale)
                 << "--group-idx" << QString::number(_resolution)
                 << "--num-slices" << QString::number(_layers);
            break;
            
        case Tool::GrowSegFromSegment:
            args << _volumePath
                 << _srcDir
                 << _tgtDir
                 << _jsonParams
                 << _srcSegment;
            break;
            
        case Tool::GrowSegFromSeeds:
            args << _volumePath
                 << _tgtDir
                 << _jsonParams;
            
            // Only add coordinates if not in expand mode and not using random seeding
            if (!_useExpandMode && !_useRandomSeed) {
                args << QString::number(_seed_x)
                     << QString::number(_seed_y)
                     << QString::number(_seed_z);
            }
            break;
        
        case Tool::SegAddOverlap:
            args << _tgtDir
                 << _tifxyzPath;
            break;
        
        case Tool::tifxyz2obj:
            args << _tifxyzPath
                 << _objPath;
            break;
    }
    
    return args;
}

QString CommandLineToolRunner::toolName(Tool tool) const
{
    // find out where vc3d is running from , so we can use full path for tool calling
    QString basePath = QCoreApplication::applicationDirPath() + "/";
    
    switch (tool) {
        case Tool::RenderTifXYZ:
            return basePath + "vc_render_tifxyz";
            
        case Tool::GrowSegFromSegment:
            return basePath + "vc_grow_seg_from_segments";
            
        case Tool::GrowSegFromSeeds:
            return basePath + "vc_grow_seg_from_seed";
        
        case Tool::SegAddOverlap:
            return basePath + "vc_seg_add_overlap";
        
        case Tool::tifxyz2obj:
            return basePath + "vc_tifxyz2obj";
            
        default:
            return "unknown_tool";
    }
}

QString CommandLineToolRunner::getOutputPath() const
{
    QFileInfo outputInfo(_outputPattern);
    return outputInfo.dir().path();
}

} // namespace ChaoVis
