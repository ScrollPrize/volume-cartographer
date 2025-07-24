add_subdirectory(VC3D)

add_executable(vc_render_video src/vc_render_video.cpp)
target_link_libraries(vc_render_video
    z5
    opencv_core
        vc_core
)

add_executable(vc_render_tifxyz src/vc_render_tifxyz.cpp)
target_link_libraries(vc_render_tifxyz
    z5
    opencv_core
        vc_core
)

add_executable(vc_objrefine src/ObjAlphaCompRefinement.cpp)
target_link_libraries(vc_objrefine
    z5
    opencv_highgui
        vc_core
)

add_executable(vc_grow_seg_from_seed src/vc_grow_seg_from_seed.cpp)
target_link_libraries(vc_grow_seg_from_seed
    z5
    opencv_highgui
        vc_core
)

add_executable(vc_seg_add_overlap src/vc_seg_add_overlap.cpp)
target_link_libraries(vc_seg_add_overlap
    z5
    opencv_highgui
        vc_core
)

add_executable(vc_grow_seg_from_segments src/vc_grow_seg_from_segments.cpp)
target_link_libraries(vc_grow_seg_from_segments
    z5
    opencv_highgui
        vc_core
)

add_executable(vc_tifxyz2obj src/vc_tifxyz2obj.cpp)
target_link_libraries(vc_tifxyz2obj
    opencv_highgui
        vc_core
)

add_executable(vc_tifxyz_inp_mask src/vc_tifxyz_inp_mask.cpp)
target_link_libraries(vc_tifxyz_inp_mask
    opencv_highgui
        vc_core
)

add_executable(vc_tifxyz_winding src/vc_tifxyz_winding.cpp)
target_link_libraries(vc_tifxyz_winding
    opencv_highgui
        vc_core
)

add_executable(vc_fill_quadmesh src/vc_fill_quadmesh.cpp)
target_link_libraries(vc_fill_quadmesh
    opencv_highgui
        vc_core
    ceres
)

add_executable(vc_tiffxyz_upscale_grounding src/vc_tiffxyz_upscale_grounding.cpp)
target_link_libraries(vc_tiffxyz_upscale_grounding
    opencv_highgui
        vc_core
    ceres
)
