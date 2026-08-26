add_library(
    aMemory
    STATIC
        src/fal_flash25q_port.c
)

target_include_directories(
    aMemory
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/inc"
)

target_link_libraries(
    aMemory
    PRIVATE
        aFlash25q
)
