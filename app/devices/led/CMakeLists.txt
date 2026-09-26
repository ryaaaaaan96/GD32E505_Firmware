if(NOT ADEV_LED_ENABLED)
    message(FATAL_ERROR "Status LED requires ADEV_LED_REQUESTED=ON")
endif()

target_sources(
    ${APP_TARGET}
    PRIVATE
        app_led.c
)

target_link_libraries(
    ${APP_TARGET}
    PRIVATE
        aDevLed
)

target_include_directories(
    ${APP_TARGET}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
