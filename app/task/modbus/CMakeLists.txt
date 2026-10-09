target_sources(${APP_TARGET} PRIVATE modbus_task.c)
target_include_directories(${APP_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
