target_sources(
    ${APP_TARGET}
    PRIVATE
        system_init.c
)

if(app_log_enable)
    target_sources(${APP_TARGET} PRIVATE log_service.c log_command.c)
endif()

# Flash 手动测试已完成；需要复测时取消下一行注释。
# target_sources(${APP_TARGET} PRIVATE flash_test.c)

if(app_database_enable)
    target_sources(${APP_TARGET} PRIVATE database_service.c)
    target_link_libraries(${APP_TARGET} PRIVATE aDataBase)
    if(ASHELL_ENABLE)
        target_sources(${APP_TARGET} PRIVATE database_command.c)
        target_include_directories(${APP_TARGET} PRIVATE
            "${ACLASS_PRODUCT_DIR}/config")
    endif()
endif()

target_include_directories(
    ${APP_TARGET}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(
    ${APP_TARGET}
    PRIVATE
        aOS
        aShell
        aLog
)
