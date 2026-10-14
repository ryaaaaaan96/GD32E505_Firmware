target_sources(
    ${APP_TARGET}
    PRIVATE
        system_init.c
)

if(ASHELL_ENABLE)
    target_sources(${APP_TARGET} PRIVATE command_system.c)
endif()

if(app_modbus_enable AND ASHELL_ENABLE)
    target_sources(${APP_TARGET} PRIVATE command_protocol.c)
endif()

if(app_log_enable AND ASHELL_ENABLE)
    target_sources(${APP_TARGET} PRIVATE command_log.c)
endif()

if(ABUS_ENABLE)
    target_link_libraries(${APP_TARGET} PRIVATE aBus)
    if(ASHELL_ENABLE)
        target_sources(${APP_TARGET} PRIVATE command_data_bus.c)
    endif()
endif()

# 基准只进入演示固件；不增加任务，也不修改产品点表。
if(APP_DEMO_ENABLE AND ABUS_ENABLE AND ABUS_DYNAMIC_ENABLE AND ASHELL_ENABLE)
    target_sources(${APP_TARGET} PRIVATE command_benchmark.c
        "${ACLASS_PRODUCT_DIR}/benchmarks/bench_bus.c")
    target_include_directories(${APP_TARGET} PRIVATE
        "${ACLASS_PRODUCT_DIR}/benchmarks")
endif()

# Flash 手动测试已完成；需要复测时取消下一行注释。
# target_sources(${APP_TARGET} PRIVATE command_flash.c)

if(app_database_enable)
    target_link_libraries(${APP_TARGET} PRIVATE aDataBase)
    if(ASHELL_ENABLE)
        target_sources(${APP_TARGET} PRIVATE command_database.c)
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
