add_library(aLog STATIC aLog_config.c)
target_include_directories(aLog PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(aLog PUBLIC
    ALOG_ENABLE=$<BOOL:${ALOG_ENABLE}>
    ALOG_OUTPUT_LEVEL=${ALOG_OUTPUT_LEVEL}
    ALOG_LINE_BUFFER_SIZE=${ALOG_LINE_BUFFER_SIZE}
)
target_link_libraries(aLog PUBLIC aLib PRIVATE aclass_project_options)

if(ALOG_ENABLE)
    # 官方头及配置仅私有可见；不编译官方移植模板和异步/缓冲插件。
    add_library(aLogEasyLogger OBJECT
        EasyLogger/easylogger/src/elog.c
        EasyLogger/easylogger/src/elog_utils.c
    )
    target_include_directories(aLogEasyLogger PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "${CMAKE_CURRENT_SOURCE_DIR}/EasyLogger/easylogger/inc"
    )
    target_compile_definitions(aLogEasyLogger PRIVATE
        ALOG_OUTPUT_LEVEL=${ALOG_OUTPUT_LEVEL}
        ALOG_LINE_BUFFER_SIZE=${ALOG_LINE_BUFFER_SIZE}
    )
    target_link_libraries(aLogEasyLogger PRIVATE aclass_project_options)
    target_sources(aLog PRIVATE aLog.c port/elog_port.c
        $<TARGET_OBJECTS:aLogEasyLogger>)
    target_include_directories(aLog PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "${CMAKE_CURRENT_SOURCE_DIR}/EasyLogger/easylogger/inc"
    )
    target_link_libraries(aLog PRIVATE aOS)
else()
    target_sources(aLog PRIVATE aLog_stub.c)
endif()
