add_library(
    aDevLed
    STATIC
        aDev_led.c
)

target_include_directories(
    aDevLed
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(
    aDevLed
    PUBLIC
        aDrv
        aLib
)
