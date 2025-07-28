project(libvc_core VERSION ${VC_VERSION} LANGUAGES CXX)


add_library(vc_slicing
        src/Slicing.cpp
)

add_library(vc_surface
        src/Surface.cpp
        src/SurfaceHelpers.cpp
        src/ChunkedTensor.cpp
        src/SurfaceVoxelizer.cpp
)

add_library(vc_core
        src/DiskBasedObjectBaseClass.cpp
        src/Metadata.cpp
        src/Segmentation.cpp
        src/Volume.cpp
        src/VolumePkg.cpp
        src/Logging.cpp

)

target_link_libraries(vc_slicing PUBLIC opencv_core)
target_link_libraries(vc_surface PUBLIC opencv_core vc_slicing ${GKLIB_LIBRARY} ${METIS_LIBRARY} Ceres::ceres OpenMP::OpenMP_CXX)
target_link_libraries(vc_core PUBLIC opencv_core)