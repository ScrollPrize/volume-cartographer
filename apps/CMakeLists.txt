############
# GUI Apps #
############
if(VC_BUILD_GUI)
add_subdirectory(VC3D)
endif()

#################
# CMD Line Apps #
#################

add_executable(vc_render_video src/vc_render_video.cpp)
target_link_libraries(vc_render_video
    z5
    opencv_core
    VC::slicing
    VC::surface
)

add_executable(vc_render_tifxyz src/vc_render_tifxyz.cpp)
target_link_libraries(vc_render_tifxyz
    z5
    opencv_core
    VC::slicing
    VC::surface
)

add_executable(vc_objrefine src/ObjAlphaCompRefinement.cpp)
target_link_libraries(vc_objrefine
    z5
    opencv_highgui
    VC::slicing
    VC::surface
)

add_executable(vc_grow_seg_from_seed src/vc_grow_seg_from_seed.cpp)
target_link_libraries(vc_grow_seg_from_seed
    z5
    opencv_highgui
    VC::slicing
    VC::surface
)

add_executable(vc_seg_add_overlap src/vc_seg_add_overlap.cpp)
target_link_libraries(vc_seg_add_overlap
    z5
    opencv_highgui
    VC::slicing
    VC::surface
)

add_executable(vc_grow_seg_from_segments src/vc_grow_seg_from_segments.cpp)
target_link_libraries(vc_grow_seg_from_segments
    z5
    opencv_highgui
    VC::slicing
    VC::surface
)

add_executable(vc_tifxyz2obj src/vc_tifxyz2obj.cpp)
target_link_libraries(vc_tifxyz2obj
    opencv_highgui
    VC::slicing
    VC::surface
)

add_executable(vc_tifxyz_inp_mask src/vc_tifxyz_inp_mask.cpp)
target_link_libraries(vc_tifxyz_inp_mask
    opencv_highgui
    VC::slicing
    VC::surface
)

add_executable(vc_tifxyz_winding src/vc_tifxyz_winding.cpp)
target_link_libraries(vc_tifxyz_winding
    opencv_highgui
    VC::slicing
    VC::surface
)

add_executable(vc_fill_quadmesh src/vc_fill_quadmesh.cpp)
target_link_libraries(vc_fill_quadmesh
    opencv_highgui
    VC::slicing
    VC::surface
    ceres
)

add_executable(vc_tiffxyz_upscale_grounding src/vc_tiffxyz_upscale_grounding.cpp)
target_link_libraries(vc_tiffxyz_upscale_grounding
    opencv_highgui
    VC::slicing
    VC::surface
    ceres
)

## experiements for flattening ##
# add_executable(vc_zarralphacomp src/ZarrAlphaComp.cpp)
# target_link_libraries(vc_zarralphacomp
#     nlohmann_json::nlohmann_json
#     z5
#     opencv_highgui
#     VC::slicing
# )

# Install targets
if(VC_INSTALL_APPS)
install(
    TARGETS
        vc_render_video
        vc_render_tifxyz
        vc_objrefine
        vc_grow_seg_from_seed
        vc_grow_seg_from_segments
        vc_tifxyz2obj
        vc_tifxyz_winding
        vc_fill_quadmesh
        ${extra_apps}
    RUNTIME DESTINATION bin
    COMPONENT Programs
)
endif()
