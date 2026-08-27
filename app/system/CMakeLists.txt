target_sources(
    ${APP_TARGET}
    PRIVATE
        aclass_system.c
)

target_include_directories(
    ${APP_TARGET}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(
    ${APP_TARGET}
    PRIVATE
        ASYSTEM_SHELL_ENABLED=$<BOOL:${APP_ENABLE_SHELL}>
)

target_link_libraries(
    ${APP_TARGET}
    PRIVATE
        aDevLed
        aOS
        aShell
)

if(APP_ENABLE_SHELL)
    target_link_libraries(
        ${APP_TARGET}
        PRIVATE
            aDevUsart
    )
endif()
