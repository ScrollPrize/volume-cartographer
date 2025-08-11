########
# Core #
########


### Qt6 ###
if((VC_BUILD_APPS OR VC_BUILD_UTILS) AND VC_BUILD_GUI)
    find_package(Qt6 QUIET REQUIRED COMPONENTS Widgets Gui Core Network)
    # qt_standard_project_setup() #NOTE below settings for QT < 6.3, commented command for qt >= 6.3, ubuntu 22.04 has qt 6.2!
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
     
    if(NOT DEFINED qt_generate_deploy_app_script)
        message(WARNING "WARNING qt_generate_deploy_app_script MISSING!")
        function(qt_generate_deploy_app_script)
        endfunction()
    endif()
     
endif()

option(VC_WITH_CUDA_SPARSE "use cudss" ON)
if (VC_WITH_CUDA_SPARSE)
    add_definitions(-DVC_USE_CUDA_SPARSE=1)
endif()

### ceres-solver ###
find_package(Ceres REQUIRED)

### Z5 ###
include(BuildZ5)

### Eigen ###
find_package(Eigen3 3.3 REQUIRED)
if(CMAKE_GENERATOR MATCHES "Ninja|.*Makefiles.*" AND "${CMAKE_BUILD_TYPE}" MATCHES "^$|Debug")
    message(AUTHOR_WARNING "Configuring a Debug build. Eigen performance will be degraded. If you need debug symbols, \
    consider setting CMAKE_BUILD_TYPE to RelWithDebInfo. Otherwise, set to Release to maximize performance.")
endif()

### OpenCV ###
find_package(OpenCV 3 QUIET)
if(NOT OpenCV_FOUND)
    find_package(OpenCV 4 QUIET REQUIRED)
endif()

set(XTENSOR_USE_XSIMD 1)
set(XTENSOR_USE_OPENMP 1)
find_package(xtensor REQUIRED)

find_package(OpenMP REQUIRED)

### spdlog ###
find_package(spdlog 1.4.2 CONFIG REQUIRED)

### Modern JSON ###
include(BuildJSON)

### CURL library (needed for GDAL) ###
find_package(CURL REQUIRED)

### Boost and indicators (for app use only) ###
if(VC_BUILD_APPS OR VC_BUILD_UTILS)
    find_package(Boost 1.58 REQUIRED COMPONENTS system program_options)
    include(BuildIndicators)
endif()
