cmake_minimum_required(VERSION 3.22 FATAL_ERROR)
project(VC3D VERSION 4.0.0)


set(CMAKE_CXX_FLAGS "-ggdb -Og -g3 -w -Wno-narrowing -std=c++23 -fdiagnostics-color=always")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld ")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")

list(PREPEND CMAKE_PREFIX_PATH "/home/forrest/ceres-install")

option(VC_WITH_CUDA_SPARSE "use cudss" OFF)
if (VC_WITH_CUDA_SPARSE)
    add_definitions(-DVC_USE_CUDA_SPARSE=1)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core Network)
find_package(OpenEXR REQUIRED)
find_package(OpenCV REQUIRED PATHS "${CMAKE_PREFIX_PATH}" NO_DEFAULT_PATH)
find_package(OpenMP REQUIRED)
find_package(CURL REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system program_options)
find_package(absl REQUIRED)
find_package(Ceres REQUIRED)

find_library(GKLIB_LIBRARY GKlib)


file(GLOB SOURCES "src/libvc/src/*.cpp" "src/libvc/include/vc/*.hpp" "src/libvcui/src/*.cpp" "src/libvcui/include/vcui/*.hpp" "src/VC3D/src/*.cpp")
add_executable(VC3D ${SOURCES})

target_link_libraries(VC3D PUBLIC OpenMP::OpenMP_CXX Qt6::Core Qt6::Gui Qt6::Widgets opencv_core opencv_highgui
        opencv_imgproc
        opencv_imgcodecs
        opencv_calib3d
        nlohmann_json::nlohmann_json
        CURL::libcurl
        Ceres::ceres
        Boost::program_options
        spdlog::spdlog
        ${GKLIB_LIBRARY})

target_include_directories(VC3D PUBLIC src/libvc/include src/libvcui/include)
