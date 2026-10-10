if(NOT ADEV_LED_ENABLE)
    message(FATAL_ERROR "Status LED requires ADEV_LED_ENABLE=ON")
endif()
if(NOT ADEV_LED_STATIC_ENABLE)
    message(FATAL_ERROR "Status LED requires ADEV_LED_STATIC_ENABLE=ON")
endif()

target_sources(
    ${APP_TARGET}
    PRIVATE
        system_device.c
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

if(app_log_enable)
    target_link_libraries(${APP_TARGET} PRIVATE aLog)
endif()

if(app_database_enable)
    target_sources(${APP_TARGET} PRIVATE database_config.c)
    target_include_directories(${APP_TARGET} PRIVATE
        "${ACLASS_PRODUCT_DIR}/config")
    target_link_libraries(${APP_TARGET} PRIVATE aDataBase)
endif()

if(ASHELL_ENABLE AND NOT ADEV_USART_DYNAMIC_ENABLE)
    message(FATAL_ERROR "Console requires ADEV_USART_DYNAMIC_ENABLE=ON")
endif()

target_compile_definitions(${APP_TARGET} PRIVATE
    ADEV_FLASH25Q_ENABLE=$<BOOL:${ADEV_FLASH25Q_ENABLE}>
    AMEMORY_ENABLE=$<BOOL:${AMEMORY_ENABLE}>
)
if(ADEV_FLASH25Q_ENABLE)
    target_sources(${APP_TARGET} PRIVATE flash_device.c)
    target_link_libraries(${APP_TARGET} PRIVATE aFlash25q)
endif()

if(AMEMORY_ENABLE AND ADEV_FLASH25Q_ENABLE)
    target_sources(${APP_TARGET} PRIVATE memory_config.c)
    target_include_directories(${APP_TARGET} PRIVATE
        "${ACLASS_PRODUCT_DIR}/config")
    target_link_libraries(${APP_TARGET} PRIVATE aMemory)
endif()
