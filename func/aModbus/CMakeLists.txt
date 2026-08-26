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
