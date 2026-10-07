target_link_libraries(${APP_TARGET} PRIVATE aLog)

# 当前板端演示使用 Shell；通用 aLog 不要求 Shell 或 Flash 开启。
set(app_log_enable OFF)
if(ALOG_ENABLE AND ASHELL_ENABLE)
    set(app_log_enable ON)
    target_sources(${APP_TARGET} PRIVATE app_log.c app_log_command.c)
    target_include_directories(${APP_TARGET} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${APP_TARGET} PRIVATE aShell)
endif()
target_compile_definitions(${APP_TARGET} PRIVATE
    APP_LOG_ENABLE=$<BOOL:${app_log_enable}>)
