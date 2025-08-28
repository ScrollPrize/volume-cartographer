#pragma once

#include <QDialog>
#include <QString>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSettings>

class RenderParamsDialog : public QDialog {
    Q_OBJECT
public:
    RenderParamsDialog(QWidget* parent,
                       const QString& volumePath,
                       const QString& segmentPath,
                       const QString& outputPattern,
                       double scale,
                       int groupIdx,
                       int numSlices);

    QString volumePath() const;
    QString segmentPath() const;
    QString outputPattern() const;
    double scale() const;
    int groupIdx() const;
    int numSlices() const;
    int ompThreads() const; // -1 if unset

    // Advanced
    int cropX() const;
    int cropY() const;
    int cropWidth() const;
    int cropHeight() const;
    QString affinePath() const;
    bool invertAffine() const;
    double scaleSegmentation() const;
    double rotateDegrees() const;
    int flipAxis() const; // -1 none, 0 vertical, 1 horizontal, 2 both
    bool includeTifs() const; // when output is .zarr

private:
    QLineEdit* edtVolume_{nullptr};
    QLineEdit* edtSegment_{nullptr};
    QLineEdit* edtOutput_{nullptr};
    QDoubleSpinBox* spScale_{nullptr};
    QSpinBox* spGroup_{nullptr};
    QSpinBox* spSlices_{nullptr};
    QLineEdit* edtThreads_{nullptr};

    QSpinBox* spCropX_{nullptr};
    QSpinBox* spCropY_{nullptr};
    QSpinBox* spCropW_{nullptr};
    QSpinBox* spCropH_{nullptr};
    QLineEdit* edtAffine_{nullptr};
    QCheckBox* chkInvert_{nullptr};
    QDoubleSpinBox* spScaleSeg_{nullptr};
    QDoubleSpinBox* spRotate_{nullptr};
    QComboBox* cmbFlip_{nullptr};
    QCheckBox* chkIncludeTifs_{nullptr};
};

class TraceParamsDialog : public QDialog {
    Q_OBJECT
public:
    TraceParamsDialog(QWidget* parent,
                      const QString& volumePath,
                      const QString& srcDir,
                      const QString& tgtDir,
                      const QString& jsonParams,
                      const QString& srcSegment);

    QString volumePath() const;
    QString srcDir() const;
    QString tgtDir() const;
    QString jsonParams() const;
    QString srcSegment() const;
    
    // Build a params JSON object from UI controls (merged or standalone)
    QJsonObject makeParamsJson() const;
    int ompThreads() const; // -1 if unset

private:
    QLineEdit* edtVolume_{nullptr};
    QLineEdit* edtSrcDir_{nullptr};
    QLineEdit* edtTgtDir_{nullptr};
    QLineEdit* edtJson_{nullptr};
    QLineEdit* edtSrcSegment_{nullptr};
    QLineEdit* edtThreads_{nullptr};

    // Advanced tracing parameters (parsed from JSON; defaults reflect GrowSurface.cpp)
    QCheckBox* chkFlipX_{nullptr};
    QSpinBox* spGlobalStepsWin_{nullptr};
    QDoubleSpinBox* spSrcStep_{nullptr};
    QDoubleSpinBox* spStep_{nullptr};
    QSpinBox* spMaxWidth_{nullptr};
    QDoubleSpinBox* spLocalCostInlTh_{nullptr};
    QDoubleSpinBox* spSameSurfaceTh_{nullptr};
    QDoubleSpinBox* spStraightW_{nullptr};
    QDoubleSpinBox* spStraightW3D_{nullptr};
    QDoubleSpinBox* spSlidingWScale_{nullptr};
    QDoubleSpinBox* spZLocLossW_{nullptr};
    QDoubleSpinBox* spDistLoss2DW_{nullptr};
    QDoubleSpinBox* spDistLoss3DW_{nullptr};
    QDoubleSpinBox* spStraightMinCount_{nullptr};
    QSpinBox* spInlierBaseTh_{nullptr};
    QCheckBox* chkZRange_{nullptr};
    QDoubleSpinBox* spZMin_{nullptr};
    QDoubleSpinBox* spZMax_{nullptr};

    // Defaults helpers
    void applyCodeDefaults();
    void applySavedDefaults();
    void saveDefaults() const;
};

class ConvertToObjDialog : public QDialog {
    Q_OBJECT
public:
    ConvertToObjDialog(QWidget* parent,
                       const QString& tifxyzPath,
                       const QString& objOutPath);

    QString tifxyzPath() const;
    QString objPath() const;
    bool normalizeUV() const;
    bool alignGrid() const;
    int decimateIterations() const;
    bool cleanSurface() const;
    int ompThreads() const; // -1 if unset

private:
    QLineEdit* edtTifxyz_{nullptr};
    QLineEdit* edtObj_{nullptr};
    QLineEdit* edtThreads_{nullptr};
    QCheckBox* chkNormalize_{nullptr};
    QCheckBox* chkAlign_{nullptr};
    QSpinBox* spDecimate_{nullptr};
    QCheckBox* chkClean_{nullptr};
};
