target_sources(
    ${APP_TARGET}
    PRIVATE
        system.c
)

target_include_directories(
    ${APP_TARGET}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(
    ${APP_TARGET}
    PRIVATE
        aDevLed
        aDevUsart
        aOS
        aShell
)
