# ---------------------------------------------------------------------------
# BuildIGL.cmake
#
# If VC_BUILD_IGL = ON   → fetch libigl at configure time with FetchContent,
#                           build only the modules we need (core + slim) and
#                           expose standard targets  igl::core  igl::slim.
#
# If VC_BUILD_IGL = OFF  → rely on a system / vcpkg / Conan install that
#                           provides libiglConfig.cmake.
# ---------------------------------------------------------------------------

option(VC_BUILD_IGL "Build in-source libigl (via FetchContent)" ON)

# ---------------------------------------------------------------------------
#  Case 1 – Build from source with FetchContent
# ---------------------------------------------------------------------------
if (VC_BUILD_IGL)
    include(FetchContent)

    # exact commit or tag; change whenever you upgrade
    set(_libigl_commit "v2.5.0")

    # disable viewer / OpenGL to keep the build small (headless CI / Docker)
    set(LIBIGL_WITH_OPENGL         OFF CACHE BOOL "" FORCE)
    set(LIBIGL_WITH_OPENGL_GLFW    OFF CACHE BOOL "" FORCE)
    set(LIBIGL_WITH_OPENGL_GLFW_IMGUI OFF CACHE BOOL "" FORCE)
    set(LIBIGL_WITH_VIEWER         OFF CACHE BOOL "" FORCE)
    # keep slim ON (needs Eigen only)
    set(LIBIGL_WITH_CGAL           OFF CACHE BOOL "" FORCE)
    set(LIBIGL_WITH_PREDICATES     OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        libigl
        GIT_REPOSITORY https://github.com/libigl/libigl.git
        GIT_TAG        ${_libigl_commit}
    )
    # Populate and automatically add_subdirectory(libigl)
    FetchContent_MakeAvailable(libigl)

    message(STATUS "libigl fetched @ ${_libigl_commit}")

# ---------------------------------------------------------------------------
#  Case 2 – Use system / vcpkg / conan package
# ---------------------------------------------------------------------------
else()
    find_package(libigl 2.5 CONFIG REQUIRED)
    message(STATUS "Using system libigl package: ${libigl_DIR}")
endif()
