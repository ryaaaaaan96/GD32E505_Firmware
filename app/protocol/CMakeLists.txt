target_sources(${APP_TARGET} PRIVATE protocol.c)
target_include_directories(${APP_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/inc")

if(ABUS_ENABLE)
    target_sources(${APP_TARGET} PRIVATE IDU_sig_table.c FAN_sig_table.c)
    target_link_libraries(${APP_TARGET} PRIVATE aBus)
endif()

# 应用示例随依赖一起裁剪；协议库仍可单独用于其他端口。
if(DEFINED APP_MODBUS_DEMO_ENABLE AND
   NOT "${APP_MODBUS_DEMO_ENABLE}" MATCHES "^(ON|OFF|TRUE|FALSE|0|1)$")
    message(FATAL_ERROR "APP_MODBUS_DEMO_ENABLE must be boolean")
endif()

set(app_modbus_enable OFF)
if(APP_MODBUS_DEMO_ENABLE AND AMODBUS_ENABLE
   AND ADEV_USART_ENABLE AND ADEV_USART_INTERRUPT_ENABLE
   AND ADEV_USART_RS485_ENABLE)
    set(app_modbus_enable ON)
    # 角色由 app_config.h 选择，协议配置平铺在本目录。
    target_sources(${APP_TARGET} PRIVATE
        FAN_modbus_master.c IDU_modbus_slave.c)
    target_link_libraries(${APP_TARGET} PRIVATE aModbus)
endif()
set(app_modbus_enable ${app_modbus_enable} PARENT_SCOPE)
target_compile_definitions(${APP_TARGET} PRIVATE
    APP_MODBUS_ENABLE=$<BOOL:${app_modbus_enable}>)
