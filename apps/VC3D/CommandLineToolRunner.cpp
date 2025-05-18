#include "CommandLineToolRunner.hpp"
#include <QDir>
#include <QFileInfo>
#include <QStatusBar>

namespace ChaoVis {

CommandLineToolRunner::CommandLineToolRunner(QStatusBar* statusBar, QObject* parent)
    : QObject(parent)
    , _progressUtil(new ProgressUtil(statusBar, this))
    , _process(nullptr)
    , _scale(1.0f)
    , _resolution(0)
    , _layers(21)
    , _seed_x(0)
    , _seed_y(0)
    , _seed_z(0)
{
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

void CommandLineToolRunner::setGrowParams(QString volumePath, QString tgtDir, QString jsonParams, int seed_x, int seed_y, int seed_z)
{
    _volumePath = volumePath;
    _tgtDir = tgtDir;
    _jsonParams = jsonParams;
    _seed_x = seed_x;
    _seed_y = seed_y;
    _seed_z = seed_z;
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
    
    if (_volumePath.isEmpty()) {
        QMessageBox::warning(nullptr, tr("Error"), tr("Volume path not specified."));
        return false;
    }
    
    if (_segmentPath.isEmpty() && (tool == Tool::RenderTifXYZ || tool == Tool::GrowSegFromSegment)) {
        QMessageBox::warning(nullptr, tr("Error"), tr("Segment path not specified."));
        return false;
    }
    
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
    
    _currentTool = tool;
    
    if (!_process) {
        _process = new QProcess(this);
        _process->setProcessChannelMode(QProcess::MergedChannels);
        
        connect(_process, &QProcess::started, this, &CommandLineToolRunner::onProcessStarted);
        connect(_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
                this, &CommandLineToolRunner::onProcessFinished);
        connect(_process, &QProcess::errorOccurred, this, &CommandLineToolRunner::onProcessError);
    }
    
    QStringList args = buildArguments(tool);
    QString toolCommand = toolName(tool);
    
    QString startMessage = tr("Starting %1 for: %2").arg(toolCommand).arg(QFileInfo(_segmentPath).fileName());
    emit toolStarted(_currentTool, startMessage);
    
    _process->start(toolCommand, args);
    
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

void CommandLineToolRunner::onProcessStarted()
{
    QString message = tr("Running %1...").arg(toolName(_currentTool));
    _progressUtil->startAnimation(message);
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
            _progressUtil->stopAnimation(message + tr(" - Path copied to clipboard"));
        } else {
            _progressUtil->stopAnimation(message);
        }
        
        emit toolFinished(_currentTool, true, message, outputPath, copyToClipboard);
    } else {
        QString errorMessage = tr("%1 failed with exit code: %2")
                                .arg(toolName(_currentTool))
                                .arg(exitCode);
        
        _progressUtil->stopAnimation(tr("Process failed"));
        
        emit toolFinished(_currentTool, false, errorMessage, QString(), false);
    }
}

void CommandLineToolRunner::onProcessError(QProcess::ProcessError error)
{
    QString errorMessage = tr("Error running %1: ").arg(toolName(_currentTool));
    
    switch (error) {
        case QProcess::FailedToStart: errorMessage += tr("failed to start"); break;
        case QProcess::Crashed: errorMessage += tr("crashed"); break;
        case QProcess::Timedout: errorMessage += tr("timed out"); break;
        case QProcess::WriteError: errorMessage += tr("write error"); break;
        case QProcess::ReadError: errorMessage += tr("read error"); break;
        default: errorMessage += tr("unknown error"); break;
    }
    
    _progressUtil->stopAnimation(tr("Process failed"));
    
    emit toolFinished(_currentTool, false, errorMessage, QString(), false);
}

QStringList CommandLineToolRunner::buildArguments(Tool tool)
{
    QStringList args;
    
    switch (tool) {
        case Tool::RenderTifXYZ:
            args << _volumePath
                 << _outputPattern
                 << _segmentPath
                 << QString::number(_scale)
                 << QString::number(_resolution)
                 << QString::number(_layers);
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
                 << _jsonParams
                 << QString::number(_seed_x)
                 << QString::number(_seed_y)
                 << QString::number(_seed_z);
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
    switch (tool) {
        case Tool::RenderTifXYZ:
            return "vc_render_tifxyz";
            
        case Tool::GrowSegFromSegment:
            return "vc_grow_seg_from_segment";
            
        case Tool::GrowSegFromSeeds:
            return "vc_grow_seg_from_seeds";
        
        case Tool::SegAddOverlap:
            return "vc_seg_add_overlap";
        
        case Tool::tifxyz2obj:
            return "vc_tifxyz2obj";
            
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
