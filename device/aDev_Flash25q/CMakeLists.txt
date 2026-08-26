add_library(
    aFlash25q
    STATIC
        aDev_flash25q.c
)

target_include_directories(
    aFlash25q
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(
    aFlash25q
    PUBLIC
        aDrv
        aLib
    PRIVATE
        aOS
)
