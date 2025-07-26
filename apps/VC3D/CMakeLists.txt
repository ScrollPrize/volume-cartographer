set(target VC3D)
set(CMAKE_AUTORCC ON)

file(GLOB SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_executable(VC3D ${SOURCES})


target_link_libraries(VC3D
    vc_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    OpenMP::OpenMP_CXX
        Boost::program_options
        spdlog::spdlog

)
