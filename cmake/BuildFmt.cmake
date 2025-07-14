option(VC_BUILD_FMT "Build in-source fmt library" on)
if(VC_BUILD_FMT)
    FetchContent_Declare(
        fmt
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        URL https://github.com/fmtlib/fmt/archive/refs/tags/10.2.1.tar.gz
    )

    FetchContent_GetProperties(fmt)
    if(NOT fmt_POPULATED)
        set(FMT_INSTALL ON CACHE INTERNAL "")
        set(FMT_TEST OFF CACHE INTERNAL "")
        FetchContent_Populate(fmt)
        add_subdirectory(${fmt_SOURCE_DIR} ${fmt_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
else()
    find_package(fmt REQUIRED)
endif()
