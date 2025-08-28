#include "ToolDialogs.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QGroupBox>
#include <QFontMetrics>
#include <QSizePolicy>
#include <QList>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

// ----- helper creators -----
static QWidget* pathPicker(QWidget* parent, QLineEdit*& lineOut, const QString& dialogTitle, bool dirMode) {
    auto w = new QWidget(parent);
    auto lay = new QHBoxLayout(w);
    lay->setContentsMargins(0,0,0,0);
    lineOut = new QLineEdit(w);
    auto btn = new QPushButton("…", w);
    lay->addWidget(lineOut);
    lay->addWidget(btn);
    QObject::connect(btn, &QPushButton::clicked, w, [parent, lineOut, dialogTitle, dirMode]() {
        if (dirMode) {
            const QString dir = QFileDialog::getExistingDirectory(parent, dialogTitle, lineOut->text());
            if (!dir.isEmpty()) lineOut->setText(dir);
        } else {
            const QString file = QFileDialog::getOpenFileName(parent, dialogTitle, lineOut->text());
            if (!file.isEmpty()) lineOut->setText(file);
        }
    });
    return w;
}

static void ensureDialogWidthForEdits(QDialog* dlg, const QList<QLineEdit*>& edits, int extra = 280, int maxW = 1600) {
    QFontMetrics fm(dlg->font());
    int need = 0;
    for (auto* e : edits) {
        if (!e) continue;
        e->setMinimumWidth(800); // ensure at least 800px visible for path-like text
        e->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        int w = fm.horizontalAdvance(e->text()) + 10; // small padding so text isn't tight
        need = std::max(need, w);
    }
    dlg->adjustSize();
    int target = std::min(std::max(dlg->width(), need + extra), maxW);
    dlg->resize(target, dlg->height());
}

// ================= RenderParamsDialog =================
RenderParamsDialog::RenderParamsDialog(QWidget* parent,
                                       const QString& volumePath,
                                       const QString& segmentPath,
                                       const QString& outputPattern,
                                       double scale,
                                       int groupIdx,
                                       int numSlices)
    : QDialog(parent)
{
    setWindowTitle("Render Parameters");
    auto main = new QVBoxLayout(this);

    // Basic params
    auto basicBox = new QGroupBox("Basic", this);
    auto basic = new QFormLayout(basicBox);
    basicBox->setLayout(basic);

    QWidget* volPick = pathPicker(this, edtVolume_, "Select OME-Zarr volume", true);
    edtSegment_ = new QLineEdit(this);
    QWidget* outPick = pathPicker(this, edtOutput_, "Select output (.zarr or tif pattern)", false);
    spScale_ = new QDoubleSpinBox(this); spScale_->setDecimals(3); spScale_->setRange(0.0001, 10000.0);
    spGroup_ = new QSpinBox(this); spGroup_->setRange(0, 10);
    spSlices_ = new QSpinBox(this); spSlices_->setRange(1, 1000);
    edtThreads_ = new QLineEdit(this); edtThreads_->setPlaceholderText("optional");
    edtThreads_->setValidator(new QRegularExpressionValidator(QRegularExpression("^\\s*\\d*\\s*$"), this));

    edtVolume_->setText(volumePath);
    edtSegment_->setText(segmentPath);
    edtOutput_->setText(outputPattern);
    spScale_->setValue(scale);
    spGroup_->setValue(groupIdx);
    spSlices_->setValue(numSlices);

    basic->addRow("Volume:", volPick);
    basic->addRow("Segmentation (tifxyz dir):", edtSegment_);
    chkIncludeTifs_ = new QCheckBox("Also write TIFF slices (Zarr)", this);
    chkIncludeTifs_->setChecked(false);

    basic->addRow("Output:", outPick);
    basic->addRow("", chkIncludeTifs_);
    basic->addRow("Scale (Pg):", spScale_);
    basic->addRow("Group index:", spGroup_);
    basic->addRow("Num slices:", spSlices_);
    basic->addRow("OMP threads:", edtThreads_);

    // Advanced
    auto advBox = new QGroupBox("Advanced (optional)", this);
    advBox->setCheckable(true);
    advBox->setChecked(false);
    auto adv = new QFormLayout(advBox);
    advBox->setLayout(adv);

    spCropX_ = new QSpinBox(this); spCropX_->setRange(0, 1000000);
    spCropY_ = new QSpinBox(this); spCropY_->setRange(0, 1000000);
    spCropW_ = new QSpinBox(this); spCropW_->setRange(0, 1000000); spCropW_->setValue(0);
    spCropH_ = new QSpinBox(this); spCropH_->setRange(0, 1000000); spCropH_->setValue(0);
    QWidget* affPick = pathPicker(this, edtAffine_, "Select affine JSON", false);
    chkInvert_ = new QCheckBox("Invert affine", this);
    spScaleSeg_ = new QDoubleSpinBox(this); spScaleSeg_->setDecimals(3); spScaleSeg_->setRange(0.0001, 1000.0); spScaleSeg_->setValue(1.0);
    spRotate_ = new QDoubleSpinBox(this); spRotate_->setDecimals(2); spRotate_->setRange(-360.0, 360.0); spRotate_->setValue(0.0);
    cmbFlip_ = new QComboBox(this);
    cmbFlip_->addItem("None", -1);
    cmbFlip_->addItem("Vertical", 0);
    cmbFlip_->addItem("Horizontal", 1);
    cmbFlip_->addItem("Both", 2);

    adv->addRow("Crop X:", spCropX_);
    adv->addRow("Crop Y:", spCropY_);
    adv->addRow("Crop Width:", spCropW_);
    adv->addRow("Crop Height:", spCropH_);
    adv->addRow("Affine transform:", affPick);
    adv->addRow("Invert affine:", chkInvert_);
    adv->addRow("Scale segmentation:", spScaleSeg_);
    adv->addRow("Rotate (deg):", spRotate_);
    adv->addRow("Flip:", cmbFlip_);

    // Buttons
    auto btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    main->addWidget(basicBox);
    main->addWidget(advBox);
    main->addWidget(btns);

    // Enable TIFF export only when output seems to be a Zarr
    auto updateIncludeTifsEnabled = [this]() {
        const QString t = edtOutput_->text().trimmed();
        const bool isZarr = t.endsWith(".zarr", Qt::CaseInsensitive);
        chkIncludeTifs_->setEnabled(isZarr);
        if (!isZarr) chkIncludeTifs_->setChecked(false);
    };
    updateIncludeTifsEnabled();
    connect(edtOutput_, &QLineEdit::textChanged, this, [updateIncludeTifsEnabled](const QString&){ updateIncludeTifsEnabled(); });

    ensureDialogWidthForEdits(this, QList<QLineEdit*>{ edtVolume_, edtSegment_, edtOutput_, edtAffine_ });
}

QString RenderParamsDialog::volumePath() const { return edtVolume_->text(); }
QString RenderParamsDialog::segmentPath() const { return edtSegment_->text(); }
QString RenderParamsDialog::outputPattern() const { return edtOutput_->text(); }
double RenderParamsDialog::scale() const { return spScale_->value(); }
int RenderParamsDialog::groupIdx() const { return spGroup_->value(); }
int RenderParamsDialog::numSlices() const { return spSlices_->value(); }
int RenderParamsDialog::ompThreads() const {
    const QString t = edtThreads_->text().trimmed();
    if (t.isEmpty()) return -1;
    bool ok=false; int v = t.toInt(&ok); return (ok && v>0) ? v : -1;
}
int RenderParamsDialog::cropX() const { return spCropX_->value(); }
int RenderParamsDialog::cropY() const { return spCropY_->value(); }
int RenderParamsDialog::cropWidth() const { return spCropW_->value(); }
int RenderParamsDialog::cropHeight() const { return spCropH_->value(); }
QString RenderParamsDialog::affinePath() const { return edtAffine_->text(); }
bool RenderParamsDialog::invertAffine() const { return chkInvert_->isChecked(); }
double RenderParamsDialog::scaleSegmentation() const { return spScaleSeg_->value(); }
double RenderParamsDialog::rotateDegrees() const { return spRotate_->value(); }
int RenderParamsDialog::flipAxis() const { return cmbFlip_->currentData().toInt(); }
bool RenderParamsDialog::includeTifs() const { return chkIncludeTifs_->isChecked(); }

// ================= TraceParamsDialog =================
TraceParamsDialog::TraceParamsDialog(QWidget* parent,
                                     const QString& volumePath,
                                     const QString& srcDir,
                                     const QString& tgtDir,
                                     const QString& jsonParams,
                                     const QString& srcSegment)
    : QDialog(parent)
{
    setWindowTitle("Run Trace Parameters");
    auto main = new QVBoxLayout(this);

    // Files/paths
    auto pathsBox = new QGroupBox("Paths", this);
    auto paths = new QFormLayout(pathsBox);
    pathsBox->setLayout(paths);

    QWidget* volPick = pathPicker(this, edtVolume_, "Select OME-Zarr volume", true);
    QWidget* srcPick = pathPicker(this, edtSrcDir_, "Select source directory (paths)", true);
    QWidget* tgtPick = pathPicker(this, edtTgtDir_, "Select target directory (traces)", true);
    QWidget* jsonPick = pathPicker(this, edtJson_, "Select trace params JSON", false);
    QWidget* segPick = pathPicker(this, edtSrcSegment_, "Select source segment (tifxyz dir)", true);
    edtThreads_ = new QLineEdit(this); edtThreads_->setPlaceholderText("optional");
    edtThreads_->setValidator(new QRegularExpressionValidator(QRegularExpression("^\\s*\\d*\\s*$"), this));

    edtVolume_->setText(volumePath);
    edtSrcDir_->setText(srcDir);
    edtTgtDir_->setText(tgtDir);
    edtJson_->setText(jsonParams);
    edtSrcSegment_->setText(srcSegment);

    paths->addRow("Volume:", volPick);
    paths->addRow("Source dir:", srcPick);
    paths->addRow("Target dir:", tgtPick);
    paths->addRow("JSON params:", jsonPick);
    paths->addRow("Source segment:", segPick);
    paths->addRow("OMP threads:", edtThreads_);

    // Advanced params
    auto advBox = new QGroupBox("Tracing Parameters", this);
    auto adv = new QFormLayout(advBox);
    advBox->setLayout(adv);

    chkFlipX_ = new QCheckBox("Flip X after first gen", this);
    spGlobalStepsWin_ = new QSpinBox(this); spGlobalStepsWin_->setRange(0, 1000000); spGlobalStepsWin_->setValue(0);
    spSrcStep_ = new QDoubleSpinBox(this); spSrcStep_->setRange(0.01, 1e6); spSrcStep_->setDecimals(3); spSrcStep_->setValue(20.0);
    spStep_ = new QDoubleSpinBox(this); spStep_->setRange(0.01, 1e6); spStep_->setDecimals(3); spStep_->setValue(10.0);
    spMaxWidth_ = new QSpinBox(this); spMaxWidth_->setRange(1, 100000000); spMaxWidth_->setValue(80000);

    spLocalCostInlTh_ = new QDoubleSpinBox(this); spLocalCostInlTh_->setRange(0.0, 1000.0); spLocalCostInlTh_->setDecimals(4); spLocalCostInlTh_->setValue(0.2);
    spSameSurfaceTh_ = new QDoubleSpinBox(this); spSameSurfaceTh_->setRange(0.0, 1000.0); spSameSurfaceTh_->setDecimals(4); spSameSurfaceTh_->setValue(2.0);
    spStraightW_ = new QDoubleSpinBox(this); spStraightW_->setRange(0.0, 1000.0); spStraightW_->setDecimals(4); spStraightW_->setValue(0.7);
    spStraightW3D_ = new QDoubleSpinBox(this); spStraightW3D_->setRange(0.0, 1000.0); spStraightW3D_->setDecimals(4); spStraightW3D_->setValue(4.0);
    spSlidingWScale_ = new QDoubleSpinBox(this); spSlidingWScale_->setRange(0.0, 1000.0); spSlidingWScale_->setDecimals(3); spSlidingWScale_->setValue(1.0);
    spZLocLossW_ = new QDoubleSpinBox(this); spZLocLossW_->setRange(0.0, 1000.0); spZLocLossW_->setDecimals(4); spZLocLossW_->setValue(0.1);
    spDistLoss2DW_ = new QDoubleSpinBox(this); spDistLoss2DW_->setRange(0.0, 1000.0); spDistLoss2DW_->setDecimals(4); spDistLoss2DW_->setValue(1.0);
    spDistLoss3DW_ = new QDoubleSpinBox(this); spDistLoss3DW_->setRange(0.0, 1000.0); spDistLoss3DW_->setDecimals(4); spDistLoss3DW_->setValue(2.0);
    spStraightMinCount_ = new QDoubleSpinBox(this); spStraightMinCount_->setRange(0.0, 1000.0); spStraightMinCount_->setDecimals(3); spStraightMinCount_->setValue(1.0);
    spInlierBaseTh_ = new QSpinBox(this); spInlierBaseTh_->setRange(0, 1000000); spInlierBaseTh_->setValue(20);

    chkZRange_ = new QCheckBox("Enforce Z range", this);
    spZMin_ = new QDoubleSpinBox(this); spZMin_->setRange(-1e9, 1e9); spZMin_->setDecimals(3);
    spZMax_ = new QDoubleSpinBox(this); spZMax_->setRange(-1e9, 1e9); spZMax_->setDecimals(3);

    adv->addRow("Flip X:", chkFlipX_);
    adv->addRow("Global steps/window:", spGlobalStepsWin_);
    adv->addRow("Source step:", spSrcStep_);
    adv->addRow("Step:", spStep_);
    adv->addRow("Max width:", spMaxWidth_);
    adv->addRow("Local cost inlier th:", spLocalCostInlTh_);
    adv->addRow("Same-surface th:", spSameSurfaceTh_);
    adv->addRow("Straight weight (2D):", spStraightW_);
    adv->addRow("Straight weight (3D):", spStraightW3D_);
    adv->addRow("Sliding window scale:", spSlidingWScale_);
    adv->addRow("Z-loc loss w:", spZLocLossW_);
    adv->addRow("Dist loss 2D w:", spDistLoss2DW_);
    adv->addRow("Dist loss 3D w:", spDistLoss3DW_);
    adv->addRow("Straight min count:", spStraightMinCount_);
    adv->addRow("Inlier base threshold:", spInlierBaseTh_);
    adv->addRow("Use Z range:", chkZRange_);
    adv->addRow("Z min:", spZMin_);
    adv->addRow("Z max:", spZMax_);

    // Apply saved defaults (overrides code defaults), then overlay JSON if present
    applySavedDefaults();

    // Prefill from JSON if present
    if (!jsonParams.isEmpty()) {
        QFile f(jsonParams);
        if (f.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(f.readAll());
            f.close();
            if (doc.isObject()) {
                const auto o = doc.object();
                chkFlipX_->setChecked(o.value("flip_x").toInt(0) != 0);
                spGlobalStepsWin_->setValue(o.value("global_steps_per_window").toInt(0));
                spSrcStep_->setValue(o.value("src_step").toDouble(20.0));
                spStep_->setValue(o.value("step").toDouble(10.0));
                spMaxWidth_->setValue(o.value("max_width").toInt(80000));
                spLocalCostInlTh_->setValue(o.value("local_cost_inl_th").toDouble(0.2));
                spSameSurfaceTh_->setValue(o.value("same_surface_th").toDouble(2.0));
                spStraightW_->setValue(o.value("straight_weight").toDouble(0.7));
                spStraightW3D_->setValue(o.value("straight_weight_3D").toDouble(4.0));
                spSlidingWScale_->setValue(o.value("sliding_w_scale").toDouble(1.0));
                spZLocLossW_->setValue(o.value("z_loc_loss_w").toDouble(0.1));
                spDistLoss2DW_->setValue(o.value("dist_loss_2d_w").toDouble(1.0));
                spDistLoss3DW_->setValue(o.value("dist_loss_3d_w").toDouble(2.0));
                spStraightMinCount_->setValue(o.value("straight_min_count").toDouble(1.0));
                spInlierBaseTh_->setValue(o.value("inlier_base_threshold").toInt(20));
                if (o.contains("z_range") && o.value("z_range").isArray()) {
                    const auto a = o.value("z_range").toArray();
                    if (a.size() == 2) {
                        chkZRange_->setChecked(true);
                        spZMin_->setValue(a[0].toDouble());
                        spZMax_->setValue(a[1].toDouble());
                    }
                } else if (o.contains("z_min") && o.contains("z_max")) {
                    chkZRange_->setChecked(true);
                    spZMin_->setValue(o.value("z_min").toDouble());
                    spZMax_->setValue(o.value("z_max").toDouble());
                }
            }
        }
    }

    // Buttons
    auto btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto btnReset = btns->addButton("Reset to Defaults", QDialogButtonBox::ResetRole);
    auto btnSave  = btns->addButton("Save as Default", QDialogButtonBox::ActionRole);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(btnReset, &QPushButton::clicked, this, [this]() {
        // Prefer saved defaults if available; otherwise code defaults
        QSettings s("VC.ini", QSettings::IniFormat);
        s.beginGroup("trace/defaults");
        const bool hasAny = s.allKeys().size() > 0;
        s.endGroup();
        if (hasAny) applySavedDefaults(); else applyCodeDefaults();
    });
    connect(btnSave, &QPushButton::clicked, this, [this]() { saveDefaults(); });

    main->addWidget(pathsBox);
    main->addWidget(advBox);
    main->addWidget(btns);

    ensureDialogWidthForEdits(this, QList<QLineEdit*>{ edtVolume_, edtSrcDir_, edtTgtDir_, edtJson_, edtSrcSegment_ });
}

QString TraceParamsDialog::volumePath() const { return edtVolume_->text(); }
QString TraceParamsDialog::srcDir() const { return edtSrcDir_->text(); }
QString TraceParamsDialog::tgtDir() const { return edtTgtDir_->text(); }
QString TraceParamsDialog::jsonParams() const { return edtJson_->text(); }
QString TraceParamsDialog::srcSegment() const { return edtSrcSegment_->text(); }
int TraceParamsDialog::ompThreads() const {
    const QString t = edtThreads_->text().trimmed();
    if (t.isEmpty()) return -1;
    bool ok=false; int v = t.toInt(&ok); return (ok && v>0) ? v : -1;
}

QJsonObject TraceParamsDialog::makeParamsJson() const {
    QJsonObject o;
    o["flip_x"] = chkFlipX_->isChecked() ? 1 : 0;
    o["global_steps_per_window"] = spGlobalStepsWin_->value();
    o["src_step"] = spSrcStep_->value();
    o["step"] = spStep_->value();
    o["max_width"] = spMaxWidth_->value();

    o["local_cost_inl_th"] = spLocalCostInlTh_->value();
    o["same_surface_th"] = spSameSurfaceTh_->value();
    o["straight_weight"] = spStraightW_->value();
    o["straight_weight_3D"] = spStraightW3D_->value();
    o["sliding_w_scale"] = spSlidingWScale_->value();
    o["z_loc_loss_w"] = spZLocLossW_->value();
    o["dist_loss_2d_w"] = spDistLoss2DW_->value();
    o["dist_loss_3d_w"] = spDistLoss3DW_->value();
    o["straight_min_count"] = spStraightMinCount_->value();
    o["inlier_base_threshold"] = spInlierBaseTh_->value();

    if (chkZRange_->isChecked()) {
        QJsonArray zr; zr.append(spZMin_->value()); zr.append(spZMax_->value());
        o["z_range"] = zr;
    }
    return o;
}

// ==== Defaults helpers ====
void TraceParamsDialog::applyCodeDefaults() {
    chkFlipX_->setChecked(false);
    spGlobalStepsWin_->setValue(0);
    spSrcStep_->setValue(20.0);
    spStep_->setValue(10.0);
    spMaxWidth_->setValue(80000);
    spLocalCostInlTh_->setValue(0.2);
    spSameSurfaceTh_->setValue(2.0);
    spStraightW_->setValue(0.7);
    spStraightW3D_->setValue(4.0);
    spSlidingWScale_->setValue(1.0);
    spZLocLossW_->setValue(0.1);
    spDistLoss2DW_->setValue(1.0);
    spDistLoss3DW_->setValue(2.0);
    spStraightMinCount_->setValue(1.0);
    spInlierBaseTh_->setValue(20);
    chkZRange_->setChecked(false);
    spZMin_->setValue(0.0);
    spZMax_->setValue(0.0);
}

void TraceParamsDialog::applySavedDefaults() {
    QSettings s("VC.ini", QSettings::IniFormat);
    s.beginGroup("trace/defaults");
    chkFlipX_->setChecked(s.value("flip_x", chkFlipX_->isChecked()).toInt() != 0);
    spGlobalStepsWin_->setValue(s.value("global_steps_per_window", spGlobalStepsWin_->value()).toInt());
    spSrcStep_->setValue(s.value("src_step", spSrcStep_->value()).toDouble());
    spStep_->setValue(s.value("step", spStep_->value()).toDouble());
    spMaxWidth_->setValue(s.value("max_width", spMaxWidth_->value()).toInt());

    spLocalCostInlTh_->setValue(s.value("local_cost_inl_th", spLocalCostInlTh_->value()).toDouble());
    spSameSurfaceTh_->setValue(s.value("same_surface_th", spSameSurfaceTh_->value()).toDouble());
    spStraightW_->setValue(s.value("straight_weight", spStraightW_->value()).toDouble());
    spStraightW3D_->setValue(s.value("straight_weight_3D", spStraightW3D_->value()).toDouble());
    spSlidingWScale_->setValue(s.value("sliding_w_scale", spSlidingWScale_->value()).toDouble());
    spZLocLossW_->setValue(s.value("z_loc_loss_w", spZLocLossW_->value()).toDouble());
    spDistLoss2DW_->setValue(s.value("dist_loss_2d_w", spDistLoss2DW_->value()).toDouble());
    spDistLoss3DW_->setValue(s.value("dist_loss_3d_w", spDistLoss3DW_->value()).toDouble());
    spStraightMinCount_->setValue(s.value("straight_min_count", spStraightMinCount_->value()).toDouble());
    spInlierBaseTh_->setValue(s.value("inlier_base_threshold", spInlierBaseTh_->value()).toInt());

    const bool useZR = s.value("use_z_range", chkZRange_->isChecked()).toBool();
    chkZRange_->setChecked(useZR);
    spZMin_->setValue(s.value("z_min", spZMin_->value()).toDouble());
    spZMax_->setValue(s.value("z_max", spZMax_->value()).toDouble());
    s.endGroup();
}

void TraceParamsDialog::saveDefaults() const {
    QSettings s("VC.ini", QSettings::IniFormat);
    s.beginGroup("trace/defaults");
    s.setValue("flip_x", chkFlipX_->isChecked() ? 1 : 0);
    s.setValue("global_steps_per_window", spGlobalStepsWin_->value());
    s.setValue("src_step", spSrcStep_->value());
    s.setValue("step", spStep_->value());
    s.setValue("max_width", spMaxWidth_->value());

    s.setValue("local_cost_inl_th", spLocalCostInlTh_->value());
    s.setValue("same_surface_th", spSameSurfaceTh_->value());
    s.setValue("straight_weight", spStraightW_->value());
    s.setValue("straight_weight_3D", spStraightW3D_->value());
    s.setValue("sliding_w_scale", spSlidingWScale_->value());
    s.setValue("z_loc_loss_w", spZLocLossW_->value());
    s.setValue("dist_loss_2d_w", spDistLoss2DW_->value());
    s.setValue("dist_loss_3d_w", spDistLoss3DW_->value());
    s.setValue("straight_min_count", spStraightMinCount_->value());
    s.setValue("inlier_base_threshold", spInlierBaseTh_->value());

    s.setValue("use_z_range", chkZRange_->isChecked());
    s.setValue("z_min", spZMin_->value());
    s.setValue("z_max", spZMax_->value());
    s.endGroup();
}

// ================= ConvertToObjDialog =================
ConvertToObjDialog::ConvertToObjDialog(QWidget* parent,
                                       const QString& tifxyzPath,
                                       const QString& objOutPath)
    : QDialog(parent)
{
    setWindowTitle("Convert to OBJ");
    auto main = new QVBoxLayout(this);
    auto form = new QFormLayout();

    QWidget* tifPick = pathPicker(this, edtTifxyz_, "Select TIFXYZ directory", true);
    QWidget* objPick = pathPicker(this, edtObj_, "Select output OBJ file", false);
    chkNormalize_ = new QCheckBox("Normalize UV to [0,1]", this);
    chkAlign_ = new QCheckBox("Align grid (flatten Z per row)", this);
    spDecimate_ = new QSpinBox(this); spDecimate_->setRange(0, 10); spDecimate_->setValue(0);
    chkClean_ = new QCheckBox("Clean surface outliers", this);
    edtThreads_ = new QLineEdit(this); edtThreads_->setPlaceholderText("optional");
    edtThreads_->setValidator(new QRegularExpressionValidator(QRegularExpression("^\\s*\\d*\\s*$"), this));

    edtTifxyz_->setText(tifxyzPath);
    edtObj_->setText(objOutPath);

    form->addRow("TIFXYZ dir:", tifPick);
    form->addRow("OBJ file:", objPick);
    form->addRow("Decimate iters:", spDecimate_);
    form->addRow("", chkNormalize_);
    form->addRow("", chkAlign_);
    form->addRow("", chkClean_);
    form->addRow("OMP threads:", edtThreads_);

    auto btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    main->addLayout(form);
    main->addWidget(btns);

    ensureDialogWidthForEdits(this, QList<QLineEdit*>{ edtTifxyz_, edtObj_ });
}

QString ConvertToObjDialog::tifxyzPath() const { return edtTifxyz_->text(); }
QString ConvertToObjDialog::objPath() const { return edtObj_->text(); }
bool ConvertToObjDialog::normalizeUV() const { return chkNormalize_->isChecked(); }
bool ConvertToObjDialog::alignGrid() const { return chkAlign_->isChecked(); }
int ConvertToObjDialog::decimateIterations() const { return spDecimate_->value(); }
bool ConvertToObjDialog::cleanSurface() const { return chkClean_->isChecked(); }
int ConvertToObjDialog::ompThreads() const {
    const QString t = edtThreads_->text().trimmed();
    if (t.isEmpty()) return -1;
    bool ok=false; int v = t.toInt(&ok); return (ok && v>0) ? v : -1;
}
