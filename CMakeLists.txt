cmake_minimum_required(VERSION 3.22 FATAL_ERROR)
project(VC3D VERSION 4.0.0)


set(CMAKE_CXX_FLAGS "-ggdb -Og -g3 -w -Wno-narrowing -std=c++23 -fdiagnostics-color=always -DWITH_BLOSC=1 -DWITH_ZLIB=ON -DWITH_XZ=1 ")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld  ")
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

find_library(BLOSC_LIBRARY blosc PATHS "${CMAKE_PREFIX_PATH}/lib" NO_DEFAULT_PATH)
find_package(LibLZMA REQUIRED)

find_library(GKLIB_LIBRARY GKlib)

set(LIBRARIES OpenMP::OpenMP_CXX opencv_core opencv_highgui
        opencv_imgproc
        opencv_imgcodecs
        opencv_calib3d
        nlohmann_json::nlohmann_json
        CURL::libcurl
        Ceres::ceres
        Boost::program_options
        spdlog::spdlog
        ${GKLIB_LIBRARY}
        ${BLOSC_LIBRARY}
        LibLZMA::LibLZMA)

set(UI_LIBRARIES Qt6::Core Qt6::Gui Qt6::Widgets)

file(GLOB SOURCES "src/libvc/src/*.cpp" "src/libvc/include/vc/*.hpp")
add_library(libvc ${SOURCES})
target_include_directories(libvc PUBLIC src/libvc/include ${LIBRARIES})
target_link_libraries(libvc PUBLIC ${LIBRARIES})

file(GLOB SOURCES "src/libvcui/src/*.cpp" "src/libvcui/include/vcui/*.hpp")
add_library(libvcui ${SOURCES})
target_link_libraries(libvcui PUBLIC libvc ${LIBRARIES} ${UI_LIBRARIES})
target_include_directories(libvcui PUBLIC src/libvcui/include libvc ${LIBRARIES} ${UI_LIBRARIES})

file(GLOB SOURCES "src/VC3D/src/*.cpp")
add_executable(VC3D ${SOURCES})

target_link_libraries(VC3D PUBLIC libvc libvcui ${LIBRARIES} ${UI_LIBRARIES})

target_include_directories(VC3D PUBLIC libvc libvcui ${LIBRARIES} ${UI_LIBRARIES})




add_executable(vc_render_video src/apps/src/vc_render_video.cpp)
target_link_libraries(vc_render_video libvc)

add_executable(vc_render_tifxyz src/apps/src/vc_render_tifxyz.cpp)
target_link_libraries(vc_render_tifxyz libvc)

add_executable(vc_objrefine src/apps/src/ObjAlphaCompRefinement.cpp)
target_link_libraries(vc_objrefine libvc)

add_executable(vc_grow_seg_from_seed src/apps/src/vc_grow_seg_from_seed.cpp)
target_link_libraries(vc_grow_seg_from_seed libvc)

add_executable(vc_seg_add_overlap src/apps/src/vc_seg_add_overlap.cpp)
target_link_libraries(vc_seg_add_overlap libvc)

add_executable(vc_grow_seg_from_segments src/apps/src/vc_grow_seg_from_segments.cpp)
target_link_libraries(vc_grow_seg_from_segments libvc)

add_executable(vc_tifxyz2obj src/apps/src/vc_tifxyz2obj.cpp)
target_link_libraries(vc_tifxyz2obj libvc)

add_executable(vc_tifxyz_inp_mask src/apps/src/vc_tifxyz_inp_mask.cpp)
target_link_libraries(vc_tifxyz_inp_mask libvc)

add_executable(vc_tifxyz_winding src/apps/src/vc_tifxyz_winding.cpp)
target_link_libraries(vc_tifxyz_winding libvc)

add_executable(vc_fill_quadmesh src/apps/src/vc_fill_quadmesh.cpp)
target_link_libraries(vc_fill_quadmesh libvc)

add_executable(vc_tiffxyz_upscale_grounding src/apps/src/vc_tiffxyz_upscale_grounding.cpp)
target_link_libraries(vc_tiffxyz_upscale_grounding libvc)

set(CMAKE_INSTALL_RPATH "${CMAKE_PREFIX_PATH}/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)