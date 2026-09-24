add_library(
    aModbus
    STATIC
        aModbus.c
)

target_include_directories(
    aModbus
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(
    aModbus
    PUBLIC
        aLib
)
