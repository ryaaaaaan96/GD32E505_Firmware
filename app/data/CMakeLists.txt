add_subdirectory(sig)

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
    add_subdirectory(modbus)
endif()
set(app_modbus_enable ${app_modbus_enable} PARENT_SCOPE)
target_compile_definitions(${APP_TARGET} PRIVATE
    APP_MODBUS_ENABLE=$<BOOL:${app_modbus_enable}>)
