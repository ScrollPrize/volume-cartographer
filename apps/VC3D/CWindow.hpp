#pragma once

#include <cstddef>
#include <cstdint>

#include <opencv2/core.hpp>
#include "ui_VCMain.h"

#include "CommandLineToolRunner.hpp"
#include "vc/core/util/SurfaceDef.hpp"

#define MAX_RECENT_VOLPKG 10

// Volpkg version required by this app
static constexpr int VOLPKG_MIN_VERSION = 6;
static constexpr int VOLPKG_SLICE_MIN_INDEX = 0;

// Our own forward declarations
class ChunkCache;
class Surface;
class QuadSurface;
class SurfaceMeta;
class OpChain;

namespace volcart {
    class Volume;
    class VolumePkg;
}

// Qt related forward declaration
class QMdiArea;
class OpsList;
class OpsSettings;
class SurfaceTreeWidget;
class SurfaceTreeWidgetItem;

namespace ChaoVis
{

class CVolumeViewer;
class CSurfaceCollection;
class CSegmentationEditorWindow;
class SeedingWidget;

class CWindow : public QMainWindow
{

    Q_OBJECT

public:
    enum SaveResponse : bool { Cancelled, Continue };


signals:
    void sendLocChanged(int x, int y, int z);
    void sendVolumeChanged(std::shared_ptr<volcart::Volume> vol, const std::string& volumeId);
    void sendSliceChanged(std::string,Surface*);
    void sendOpChainSelected(OpChain*);
    void sendPointsChanged(const std::vector<cv::Vec3f> red, const std::vector<cv::Vec3f> blue);
    void sendSurfacesLoaded(); 

public slots:
    void onShowStatusMessage(QString text, int timeout);
    void onLocChanged(void);
    void onManualPlaneChanged(void);
    void onVolumeClicked(cv::Vec3f vol_loc, cv::Vec3f normal, Surface *surf, Qt::MouseButton buttons, Qt::KeyboardModifiers modifiers);
    void onOpChainChanged(OpChain *chain);
    void onTagChanged(void);
    void onResetPoints(void);
    void onSurfaceContextMenuRequested(const QPoint& pos);
    void onRenderSegment(const SurfaceID& segmentId);
    void onGrowSegmentFromSegment(const SurfaceID& segmentId);
    void onAddOverlap(const SurfaceID& segmentId);
    void onConvertToObj(const SurfaceID& segmentId);
    void onGrowSeeds(const SurfaceID& segmentId, bool isExpand, bool isRandomSeed = false);
    void onToggleConsoleOutput();
    void onDeleteSegments(const std::vector<SurfaceID>& segmentIds);

public:
    CWindow();
    ~CWindow(void);
    
    // Helper method to get the current volume path
    QString getCurrentVolumePath() const;

private:
    void CreateWidgets(void);
    void CreateMenus(void);
    void CreateActions(void);

    void FillSurfaceTree(void);
    void UpdateSurfaceTreeIcon(SurfaceTreeWidgetItem *item);

    void UpdateView(void);
    void UpdateVolpkgLabel(int filterCounter);

    void UpdateRecentVolpkgActions(void);
    void UpdateRecentVolpkgList(const QString& path);
    void RemoveEntryFromRecentVolpkg(const QString& path);
    
    // Helper method for command line tools
    bool initializeCommandLineRunner(void);

    CVolumeViewer *newConnectedCVolumeViewer(std::string surfaceName, QString title, QMdiArea *mdiArea);
    void closeEvent(QCloseEvent* event);

    void setWidgetsEnabled(bool state);

    bool InitializeVolumePkg(const std::string& nVpkgPath);
    void setDefaultWindowWidth(std::shared_ptr<volcart::Volume> volume);

    void OpenVolume(const QString& path);
    void CloseVolume(void);
    void LoadSurfaces(bool reload = false);

    static void audio_callback(void *user_data, uint8_t *raw_buffer, int bytes);
    void playPing();

    void setVolume(std::shared_ptr<volcart::Volume> newvol);

private slots:
    void Open(void);
    void Open(const QString& path);
    void OpenRecent();
    void Keybindings(void);
    void About(void);
    void ShowSettings();
    void ResetSegmentationViews();
    void onSurfaceSelected();
    void onSegFilterChanged(int index);
    void onSegmentationDirChanged(int index);
    void onEditMaskPressed();
    void onRefreshSurfaces();

private:
    bool appInitComplete{false};
    std::shared_ptr<volcart::VolumePkg> fVpkg;
    Surface *_seg_surf;
    QString fVpkgPath;
    std::string fVpkgName;

    std::shared_ptr<volcart::Volume> currentVolume;
    std::string currentVolumeId;
    int loc[3] = {0,0,0};

    static const int AMPLITUDE = 28000;
    static const int FREQUENCY = 44100;

    // window components
    QMenu* fFileMenu;
    QMenu* fEditMenu;
    QMenu* fViewMenu;
    QMenu* fHelpMenu;
    QMenu* fRecentVolpkgMenu{};

    QAction* fOpenVolAct;
    QAction* fOpenRecentVolpkg[MAX_RECENT_VOLPKG]{};
    QAction* fSettingsAct;
    QAction* fExitAct;
    QAction* fKeybinds;
    QAction* fAboutAct;
    QAction* fResetMdiView;
    QAction* fShowConsoleOutputAct;

    QComboBox* volSelect;
    QComboBox* cmbFilterSegs;
    QComboBox* cmbSegmentationDir;
    
    QCheckBox* _chkApproved;
    QCheckBox* _chkDefective;
    QCheckBox* _chkReviewed;
    QCheckBox* _chkRevisit;
    QLabel* _lblPointsInfo;
    QPushButton* _btnResetPoints;
    QuadSurface *_surf;
    SurfaceID _surfID;
    
    // Seeding widget
    SeedingWidget* _seedingWidget;

    std::vector<cv::Vec3f> _red_points;
    std::vector<cv::Vec3f> _blue_points;
    
    SurfaceTreeWidget *treeWidgetSurfaces;
    OpsList *wOpsList;
    OpsSettings *wOpsSettings;
    QPushButton *btnReloadSurfaces;
    
    //TODO abstract these into separate QWidget class?
    QLabel* lblLoc[3];
    QDoubleSpinBox* spNorm[3];


    Ui_VCMainWindow ui;
    QMdiArea *mdiArea;
    QStatusBar* statusBar;

    bool can_change_volume_();
    
    ChunkCache *chunk_cache;
    std::vector<CVolumeViewer*> _viewers;
    CSurfaceCollection *_surf_col;

    std::unordered_map<std::string, OpChain*> _opchains;
    std::unordered_map<std::string, SurfaceMeta*> _vol_qsurfs;
    
    // runner for command line tools 
    CommandLineToolRunner* _cmdRunner;
};  // class CWindow

}  // namespace ChaoVis
