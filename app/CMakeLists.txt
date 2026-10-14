set(APP_TARGET ${PROJECT_NAME})

add_executable(
    ${APP_TARGET}
        main.c
)

target_include_directories(${APP_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT DEFINED APP_DEMO_ENABLE)
    set(APP_DEMO_ENABLE ON)
endif()
if(NOT "${APP_DEMO_ENABLE}" MATCHES "^(ON|OFF|0|1)$")
    message(FATAL_ERROR "APP_DEMO_ENABLE must be ON or OFF")
endif()
target_compile_definitions(${APP_TARGET} PRIVATE
    APP_DEMO_ENABLE=$<BOOL:${APP_DEMO_ENABLE}>)
set_property(TARGET ${APP_TARGET} PROPERTY APP_DEMO_ENABLE
    "${APP_DEMO_ENABLE}")

if(ADEV_USART_ENABLE)
    target_link_libraries(${APP_TARGET} PRIVATE aDevUsart)
endif()

# 当前日志输出使用控制台串口，独立于 Shell 功能裁剪。
set(app_log_enable OFF)
if(ALOG_ENABLE AND ADEV_USART_ENABLE)
    set(app_log_enable ON)
endif()
target_compile_definitions(${APP_TARGET} PRIVATE
    APP_LOG_ENABLE=$<BOOL:${app_log_enable}>)

# 本产品的存储注册表当前包含 SPI Flash；以后在此纳入其他设备。
set(app_memory_enable OFF)
if(AMEMORY_ENABLE AND ADEV_FLASH25Q_ENABLE)
    set(app_memory_enable ON)
endif()
target_compile_definitions(${APP_TARGET} PRIVATE
    APP_MEMORY_ENABLE=$<BOOL:${app_memory_enable}>)

# 数据库仅依赖本产品已注册的 aMemory 区域。
set(app_database_enable OFF)
if(ADATABASE_ENABLE AND app_memory_enable)
    set(app_database_enable ON)
endif()
target_compile_definitions(${APP_TARGET} PRIVATE
    APP_DATABASE_ENABLE=$<BOOL:${app_database_enable}>)

# 先装配 aBus 数据及其协议映射，确定设备和任务所需的资源。
add_subdirectory(protocol)
add_subdirectory(devices)
add_subdirectory(task)

target_link_libraries(
    ${APP_TARGET}
    PRIVATE
        aCore
        aDrv
        aOS
)

set_target_properties(
    ${APP_TARGET}
    PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}
        SUFFIX ".elf"
)

generate_firmware_images(${APP_TARGET})
target_link_libraries(${APP_TARGET} PRIVATE aclass_project_options)

target_compile_definitions(${APP_TARGET} PRIVATE
    ABUS_ENABLE=$<BOOL:${ABUS_ENABLE}>
)
