if(NOT ADEV_LED_ENABLED)
    message(FATAL_ERROR "Status LED requires ADEV_LED_REQUESTED=ON")
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

if(ASHELL_ENABLED AND
   (NOT ADEV_USART_ENABLED OR NOT ADEV_USART_INTERRUPT_ENABLED))
    message(FATAL_ERROR
        "Shell transport requires ADEV_USART_REQUESTED=ON "
        "and ADEV_USART_INTERRUPT_REQUESTED=ON"
    )
endif()

if(ASHELL_ENABLED)
    target_link_libraries(${APP_TARGET} PRIVATE aShell)
endif()

if(ASHELL_ENABLED AND NOT ADEV_USART_DYNAMIC_ENABLED)
    message(FATAL_ERROR "Console requires ADEV_USART_DYNAMIC_REQUESTED=ON")
endif()
