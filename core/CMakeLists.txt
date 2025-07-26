
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")

add_library(vc_core ${SOURCES})

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
        OpenMP::OpenMP_CXX
        Boost::program_options
        spdlog::spdlog
        ${GKLIB_LIBRARY}
        opencv_highgui
)

target_include_directories(vc_core PUBLIC ${CMAKE_SOURCE_DIR}/core/include)
