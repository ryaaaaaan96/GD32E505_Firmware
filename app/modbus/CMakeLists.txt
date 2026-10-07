# 应用示例随依赖一起裁剪；库仍可单独用于其他端口及主站。
set(app_modbus_enable OFF)
if(APP_MODBUS_DEMO_ENABLE AND AMODBUS_ENABLE
   AND ADEV_USART_ENABLE AND ADEV_USART_INTERRUPT_ENABLE
   AND ADEV_USART_RS485_ENABLE)
    if((APP_MODBUS_MASTER_ENABLE AND AMODBUS_CLIENT_ENABLE) OR
       (NOT APP_MODBUS_MASTER_ENABLE AND AMODBUS_SERVER_ENABLE))
        set(app_modbus_enable ON)
        target_sources(${APP_TARGET} PRIVATE app_modbus.c
            ../devices/modbus/app_modbus_port.c
            ../task/modbus/app_modbus_task.c)
        target_include_directories(${APP_TARGET} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../devices/modbus"
            "${CMAKE_CURRENT_SOURCE_DIR}/../task/modbus")
        target_link_libraries(${APP_TARGET} PRIVATE aModbus aDevUsart)
    endif()
endif()
foreach(option APP_MODBUS_DEMO_ENABLE APP_MODBUS_MASTER_ENABLE)
    if(DEFINED ${option} AND
       NOT "${${option}}" MATCHES "^(ON|OFF|TRUE|FALSE|0|1)$")
        message(FATAL_ERROR "${option} must be boolean")
    endif()
endforeach()
target_compile_definitions(${APP_TARGET} PRIVATE
    APP_MODBUS_ENABLE=$<BOOL:${app_modbus_enable}>
    APP_MODBUS_MASTER_ENABLE=$<BOOL:${APP_MODBUS_MASTER_ENABLE}>)
