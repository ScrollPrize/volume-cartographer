project(libvc_core VERSION ${VC_VERSION} LANGUAGES CXX)

set(type_srcs
    src/DiskBasedObjectBaseClass.cpp
    src/Metadata.cpp
    src/Segmentation.cpp
    src/Volume.cpp
    src/VolumePkg.cpp
)

set(logging_srcs
    src/Logging.cpp
)

set(slicing_srcs
    src/Slicing.cpp
)

add_library(vc_slicing
    ${slicing_srcs}
)
add_library(VC::slicing ALIAS vc_slicing)
target_link_libraries(vc_slicing
    PUBLIC
        z5
        opencv_highgui
        opencv_calib3d
        OpenMP::OpenMP_CXX
)
target_include_directories(vc_slicing
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set(surface_srcs
    src/Surface.cpp
    src/SurfaceHelpers.cpp
    src/ChunkedTensor.cpp
    src/SurfaceVoxelizer.cpp
)
add_library(vc_surface
    ${surface_srcs}
)
add_library(VC::surface ALIAS vc_surface)
target_link_libraries(vc_surface
    PRIVATE
        VC::slicing
        OpenMP::OpenMP_CXX
        Ceres::ceres
    PUBLIC
        opencv_core
        opencv_video
        z5
        nlohmann_json::nlohmann_json
)
target_include_directories(vc_surface
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set(metrics_srcs
    src/VCCollection.cpp
    src/surface_metrics.cpp
)

set(metrics_hdrs
    include/vc/core/util/VCCollection.hpp
)

qt_wrap_cpp(metrics_moc ${metrics_hdrs})

add_library(vc_metrics
    ${metrics_srcs}
    ${metrics_moc}
)
add_library(VC::metrics ALIAS vc_metrics)
target_link_libraries(vc_metrics
    PUBLIC
        VC::core
        Qt6::Core
)
target_include_directories(vc_metrics
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

configure_file(src/Version.cpp.in Version.cpp)

add_library(vc_core
    ${type_srcs}
    ${logging_srcs}
    ${CMAKE_CURRENT_BINARY_DIR}/Version.cpp
)
add_library(VC::core ALIAS vc_core)
target_include_directories(vc_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(vc_core
    PUBLIC
        ${VC_FS_LIB}
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        z5
        CURL::libcurl
    PRIVATE
        OpenMP::OpenMP_CXX
)

set_target_properties(vc_core PROPERTIES
    VERSION ${PROJECT_VERSION}
    EXPORT_NAME core
    POSITION_INDEPENDENT_CODE TRUE
)

## Install targets ##
if(VC_INSTALL_LIBS)
install(
    TARGETS vc_core
    COMPONENT "Libraries"
    EXPORT "${targets_export_name}"
    ARCHIVE DESTINATION "lib"
    LIBRARY DESTINATION "lib"
    INCLUDES DESTINATION "${include_install_dir}/core"
    RUNTIME DESTINATION "bin"
)
install(
    DIRECTORY "${include_install_dir}/core"
    DESTINATION "${include_install_dir}"
    COMPONENT "Libraries"
    FILES_MATCHING REGEX ".*\.(h|hpp)$"
)
endif()
