
add_library(vc_core
    src/DiskBasedObjectBaseClass.cpp
    src/Metadata.cpp
    src/Segmentation.cpp
    src/Volume.cpp
    src/VolumePkg.cpp
        src/Logging.cpp
        src/Slicing.cpp
        src/Surface.cpp
        src/SurfaceHelpers.cpp
        src/ChunkedTensor.cpp
        src/SurfaceVoxelizer.cpp
        src/ImageConversion.cpp
)

target_link_libraries(vc_core
    PUBLIC
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        CURL::libcurl
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        OpenMP::OpenMP_CXX
        opencv_core
        opencv_imgproc
        opencv_calib3d
        Ceres::ceres
)

target_include_directories(vc_core PUBLIC ${CMAKE_SOURCE_DIR}/core/include /usr/include/eigen3)
