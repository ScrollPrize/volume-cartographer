#pragma once

#include <QThread>
#include <QString>
#include <QProcess>
#include "vc/core/util/SurfaceDef.hpp"

class SegmentRenderThread : public QThread
{
    Q_OBJECT

public:
    SegmentRenderThread(QObject *parent = nullptr);
    ~SegmentRenderThread();

    void setParameters(
        const QString& volumePath,
        const QString& segmentPath, 
        const QString& outputPattern,
        float scale,
        int resolution,
        int layers);

    void run() override;

signals:
    void renderingStarted(const QString& message);
    void renderingFinished(const QString& message);
    void renderingFailed(const QString& errorMessage);

private:
    QString m_volumePath;
    QString m_segmentPath;
    QString m_outputPattern;
    float m_scale;
    int m_resolution;
    int m_layers;
    QProcess* m_process;
};
