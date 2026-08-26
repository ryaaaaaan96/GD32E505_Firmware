add_library(aLib INTERFACE)

target_include_directories(
    aLib
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
