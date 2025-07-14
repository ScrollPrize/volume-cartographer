option(VC_BUILD_SPDLOG "Build in-source spdlog library" on)
if(VC_BUILD_SPDLOG)
    FetchContent_Declare(
        spdlog
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.13.0.tar.gz
    )

    FetchContent_GetProperties(spdlog)
    if(NOT spdlog_POPULATED)
        set(SPDLOG_INSTALL ON CACHE INTERNAL "")
        set(SPDLOG_BUILD_SHARED OFF CACHE INTERNAL "")
        set(SPDLOG_FMT_EXTERNAL ON CACHE INTERNAL "")
        set(SPDLOG_BUILD_EXAMPLE OFF CACHE INTERNAL "")
        set(SPDLOG_BUILD_TESTS OFF CACHE INTERNAL "")
        FetchContent_Populate(spdlog)
        add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
else()
    find_package(spdlog 1.4.2 CONFIG REQUIRED)
endif()
