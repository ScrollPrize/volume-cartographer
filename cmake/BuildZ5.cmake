option(VC_BUILD_Z5 "Build in-source z5 header only library" on)
if(VC_BUILD_Z5)
  # Declare the project
  FetchContent_Declare(
      z5
      GIT_REPOSITORY https://github.com/constantinpape/z5.git
      GIT_TAG 7df1d21cf3e2ebee8f33f81f67096a71a97d33cd
  )

  # Populate the project but exclude from all
  FetchContent_GetProperties(z5)
  if(NOT z5_POPULATED)
    FetchContent_Populate(z5)
  endif()
  option(BUILD_Z5PY "" OFF)
  option(WITH_BLOSC "" ON)
  add_subdirectory(${z5_SOURCE_DIR} ${z5_BINARY_DIR} EXCLUDE_FROM_ALL)
  # target_link_libraries(z5 INTERFACE blosc)
else()
  find_package(z5 REQUIRED)
endif()
