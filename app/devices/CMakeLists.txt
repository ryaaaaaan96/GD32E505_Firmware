target_include_directories(
    ${APP_TARGET}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_subdirectory(system)

# 本产品的 RS485 串口随 Modbus 演示启用，设备源码不依赖协议。
if(app_modbus_enable)
    add_subdirectory(rs485)
endif()
