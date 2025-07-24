cmake_minimum_required(VERSION 3.22 FATAL_ERROR)
include(FetchContent)
include(CMakeDependentOption)

set(VC_VERSION 3.0.2)

option(VC_VERSION_DATESTAMP "Append date stamp to version number" off)
if(VC_VERSION_DATESTAMP)
string(TIMESTAMP VC_VERSION_TWEAK "%s")
set(VC_VERSION ${VC_VERSION}.${VC_VERSION_TWEAK})
endif()

project(VC3D VERSION ${VC_VERSION})

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

set(CMAKE_CXX_FLAGS "-ggdb -Og -g3 -w -Wno-narrowing")

find_program(CCACHE_PROGRAM "ccache")
if(CCACHE_FOUND)
    set(CMAKE_C_COMPILER_LAUNCHER ccache)
    set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS on)


include(VCConstants)
include(VCFindDependencies)

add_compile_options(-fdiagnostics-color=always)
add_subdirectory(core)
add_subdirectory(apps)
