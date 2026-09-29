if(NOT ADEV_LED_ENABLE)
    message(FATAL_ERROR "Status LED requires ADEV_LED_ENABLE=ON")
endif()

target_sources(
    ${APP_TARGET}
    PRIVATE
        app_system_device.c
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

if(ASHELL_ENABLE AND
   (NOT ADEV_USART_ENABLE OR NOT ADEV_USART_INTERRUPT_ENABLE))
    message(FATAL_ERROR
        "Shell transport requires ADEV_USART_ENABLE=ON "
        "and ADEV_USART_INTERRUPT_ENABLE=ON"
    )
endif()

if(ASHELL_ENABLE)
    target_link_libraries(${APP_TARGET} PRIVATE aShell)
endif()

if(ASHELL_ENABLE AND NOT ADEV_USART_DYNAMIC_ENABLE)
    message(FATAL_ERROR "Console requires ADEV_USART_DYNAMIC_ENABLE=ON")
endif()
