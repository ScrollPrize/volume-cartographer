cmake_minimum_required(VERSION 3.22 FATAL_ERROR)

project(VC3D LANGUAGES C CXX)

list(PREPEND CMAKE_PREFIX_PATH "/home/forrest/ceres-install")
list(PREPEND CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu/cmake")

include_directories(/home/forrest/ceres-install/include/ core/include)

option(VC_WITH_CUDA_SPARSE "use cudss" OFF)
if (VC_WITH_CUDA_SPARSE)
    add_definitions(-DVC_USE_CUDA_SPARSE=1)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core Network)
find_package(OpenEXR REQUIRED)
find_package(OpenCV REQUIRED)
find_package(OpenMP REQUIRED)
find_package(CURL REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system program_options)
find_package(absl REQUIRED)
find_package(Ceres REQUIRED)
find_package(VTK REQUIRED)

find_library(BLOSC_LIBRARY blosc PATHS "/home/forrest/ceres-install/lib")
find_package(LibLZMA REQUIRED)

find_library(GKLIB_LIBRARY GKlib PATHS "/home/forrest/ceres-install/lib")

find_library(METIS_LIBRARY metis PATHS "/home/forrest/ceres-install/lib")

set(LIBRARIES
        opencv_core
        opencv_highgui
        opencv_imgproc
        opencv_imgcodecs
        opencv_calib3d
        nlohmann_json::nlohmann_json
        CURL::libcurl
        Ceres::ceres
        ${METIS_LIBRARY}
        ${GKLIB_LIBRARY}
        #Boost::program_options
        spdlog::spdlog
        ${BLOSC_LIBRARY}
        LibLZMA::LibLZMA)

set(CMAKE_CXX_FLAGS " -Wno-narrowing -std=c++23 " )

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb -Og  -g3 ")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3  -flto ")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto ")
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -flto -ggdb -g3")
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os  -flto -ggdb -g3")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS on)

add_subdirectory(core)
add_subdirectory(apps)