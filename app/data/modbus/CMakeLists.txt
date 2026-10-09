# 角色由 app/app_config.h 选择；两个文件各自通过 #if 裁剪实现。
target_sources(${APP_TARGET} PRIVATE
    modbus_master.c
    modbus_slave.c)
target_include_directories(${APP_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${APP_TARGET} PRIVATE aModbusUsart)
