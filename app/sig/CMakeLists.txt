if(ABUS_ENABLE)
    target_sources(${APP_TARGET} PRIVATE app_sig.c)
    if(ASHELL_ENABLE)
        target_sources(${APP_TARGET} PRIVATE app_sig_command.c)
    endif()
    target_include_directories(${APP_TARGET} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${APP_TARGET} PRIVATE aBus)
endif()
