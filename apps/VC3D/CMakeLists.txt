# VC Main app
set(target VC3D)
set(CMAKE_AUTORCC ON)

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
    VCCollection.cpp
    VCCollection.hpp
    CPointCollectionWidget.cpp
)

set(MACOSX_BUNDLE_ICON logo.icns)
set_source_files_properties(${MACOSX_BUNDLE_ICON} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
)
if(APPLE)
    list(APPEND srcs ${MACOSX_BUNDLE_ICON})
endif()

set(copyright "Copyright 2025 EduceLab")
string(CONCAT info_string
    ${target}
    " Version ${PROJECT_VERSION}"
    ", "
    ${copyright}
)

add_executable(${target} ${srcs} resources.qrc)
set_target_properties(${target} PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME ${target}
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.educelab.VC"
    MACOSX_BUNDLE_INFO_STRING ${info_string}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_LONG_VERSION_STRING "${target}-${CMAKE_BUILD_TYPE} Version ${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE ${MACOSX_BUNDLE_ICON}
    MACOSX_BUNDLE_COPYRIGHT ${copyright}
)
if(APPLE)
    set_target_properties(${target} PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks/"
    )
endif()
target_include_directories(${target}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
find_package(OpenMP)
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
find_package(GSL REQUIRED)
include_directories(${GSL_INCLUDE_DIRS})
target_link_libraries(${target}
    VC::core
    VC::slicing
    VC::surface
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${OSXSecurity}
    OpenMP::OpenMP_CXX
    ${SDL2_LIBRARIES}
    ${GSL_LIBRARIES}
    z5
    OpenMP::OpenMP_CXX
)

if (${VC_INSTALL_APPS})
    install(
        TARGETS ${target}
        BUNDLE DESTINATION . COMPONENT Programs
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Programs
    )

    qt_generate_deploy_app_script(
        TARGET ${target}
        OUTPUT_SCRIPT vc_gui_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${vc_gui_deploy_script} COMPONENT Programs)
endif()
