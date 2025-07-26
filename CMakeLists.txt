cmake_minimum_required(VERSION 3.22 FATAL_ERROR)
project(VC3D VERSION 4.0.0)


set(CMAKE_CXX_FLAGS "-ggdb -Og -g3 -w -Wno-narrowing -std=c++20")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld ")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")

list(PREPEND CMAKE_PREFIX_PATH "/home/forrest/ceres-install")
find_program(CCACHE_PROGRAM "ccache")
if(CCACHE_FOUND)
    set(CMAKE_C_COMPILER_LAUNCHER ccache)
    set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS on)

option(VC_WITH_CUDA_SPARSE "use cudss" ON)
if (VC_WITH_CUDA_SPARSE)
    add_definitions(-DVC_USE_CUDA_SPARSE=1)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core Network)
find_package(OpenCV  REQUIRED)
find_package(OpenMP REQUIRED)
find_package(CURL REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system program_options)
find_package(absl REQUIRED)
find_package(Ceres REQUIRED)

find_library(GKLIB_LIBRARY GKlib PATHS /home/forrest/ceres-install/lib)

add_compile_options(-fdiagnostics-color=always)
add_subdirectory(core)
add_subdirectory(apps)
