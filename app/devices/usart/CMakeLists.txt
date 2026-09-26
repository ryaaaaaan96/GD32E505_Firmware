if(ASHELL_ENABLED AND
   (NOT ADEV_USART_ENABLED OR NOT ADEV_USART_INTERRUPT_ENABLED))
    message(FATAL_ERROR
        "Shell transport requires ADEV_USART_REQUESTED=ON "
        "and ADEV_USART_INTERRUPT_REQUESTED=ON"
    )
endif()

if(ADEV_USART_ENABLED)
    target_sources(
        ${APP_TARGET}
        PRIVATE
            app_usart.c
    )
    target_include_directories(
        ${APP_TARGET}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()
