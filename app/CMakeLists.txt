set(APP_TARGET ${PROJECT_NAME})

add_executable(
    ${APP_TARGET}
        main.c
)

target_include_directories(${APP_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(ADEV_USART_ENABLE)
    target_link_libraries(${APP_TARGET} PRIVATE aDevUsart)
endif()

# 当前产品的日志输出使用 Shell，设备配置与系统服务共用此启用条件。
set(app_log_enable OFF)
if(ALOG_ENABLE AND ASHELL_ENABLE)
    set(app_log_enable ON)
endif()
target_compile_definitions(${APP_TARGET} PRIVATE
    APP_LOG_ENABLE=$<BOOL:${app_log_enable}>)

# 本产品数据库使用板载 Flash，配置与服务按同一条件裁剪。
set(app_database_enable OFF)
if(ADATABASE_ENABLE AND ADEV_FLASH25Q_ENABLE)
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
