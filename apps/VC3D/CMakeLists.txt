
set(srcs
    VCAppMain.cpp
    CMesh.cpp
    CWindow.cpp
    CWindowContextMenu.cpp
    UDataManipulateUtils.cpp
    CVolumeViewer.cpp
    CVolumeViewerView.cpp
    BlockingDialog.hpp
    ColorFrame.hpp
    SettingsDialog.cpp
    CSurfaceCollection.cpp
    OpChain.cpp
    OpsList.cpp
    OpsSettings.cpp
    formsetsrc.cpp
    SurfaceTreeWidget.hpp
    SurfaceTreeWidget.cpp
    SeedingWidget.cpp
    SeedingWidget.hpp
    DrawingWidget.cpp
    DrawingWidget.hpp
    PathData.cpp
    PathData.hpp
    CSegmentationEditorWindow.cpp
    CSegmentationEditorWindow.hpp
    SegmentRenderThread.cpp
    SegmentRenderThread.hpp
    ProgressUtil.cpp
    ProgressUtil.hpp
    CommandLineToolRunner.cpp
    CommandLineToolRunner.hpp
    ConsoleOutputWidget.cpp
    ConsoleOutputWidget.hpp
)

add_executable(VC3D ${srcs})

target_link_libraries(VC3D
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    vc_core
    vc_slicing
    vc_surface
        ${LIBRARIES})
