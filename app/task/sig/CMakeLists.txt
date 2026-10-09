if(ABUS_ENABLE)
    target_sources(${APP_TARGET} PRIVATE sig_task.c)
    target_include_directories(${APP_TARGET} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
