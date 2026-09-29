# Central resolver: validate switches; never silently enable dependencies.
# First validate all inputs, then check dependencies by module.
# Application-specific requirements belong to app/CMakeLists.txt or its children.
function(_aclass_option input)
    if(NOT DEFINED ${input})
        message(FATAL_ERROR "Missing configuration input: ${input} (config/aclass_config.cmake)")
    endif()
    if(NOT "${${input}}" MATCHES "^(ON|OFF|TRUE|FALSE|0|1)$")
        message(FATAL_ERROR "${input} must be boolean, got '${${input}}'")
    endif()
    if(${input})
        set(${input} ON PARENT_SCOPE)
    else()
        set(${input} OFF PARENT_SCOPE)
    endif()
endfunction()

function(_aclass_requires requester)
    if(${requester})
        foreach(dependency IN LISTS ARGN)
            if(NOT DEFINED ${dependency})
                message(FATAL_ERROR "Unknown configuration dependency: ${dependency}")
            endif()
            if(NOT ${dependency})
                message(FATAL_ERROR
                    "${requester}=ON requires ${dependency}=ON. "
                    "Enable ${dependency} in config/aclass_config.cmake "
                    "or disable ${requester}.")
            endif()
        endforeach()
    endif()
endfunction()


# ── Inputs: validate every switch before evaluating any edge ──────────
foreach(module AOS_WORKQUEUE ABUS ABUS_DEF_CHECK ASHELL ADATABASE AMODBUS ADEV_LED ADEV_USART
        ADEV_USART_INTERRUPT ADEV_USART_DIRECT ADEV_USART_ASYNC
        ADEV_USART_RS485 ADEV_USART_STATIC ADEV_USART_DYNAMIC ADEV_FLASH25Q)
    _aclass_option(${module}_ENABLE)
endforeach()

if(ABUS_ENABLE AND NOT ABUS_LOCK_GRANULARITY MATCHES "^(NONE|BUS|SIG)$")
    message(FATAL_ERROR "ABUS_LOCK_GRANULARITY must be NONE, BUS or SIG")
endif()

foreach(module GPIO USART DMA SPI QSPI)
    _aclass_option(ADRV_MODULE_${module}_ENABLE)
endforeach()
foreach(feature INTERRUPT DMA)
    _aclass_option(ADRV_USART_${feature}_ENABLE)
endforeach()

# ── func / aShell ─────────────────────────────────────────────────────

# ── func / aDataBase ──────────────────────────────────────────────────
if(ADATABASE_ENABLE)
    if(ADATABASE_BACKEND STREQUAL "FLASH25Q")
        _aclass_requires(ADATABASE_ENABLE ADEV_FLASH25Q_ENABLE)
    elseif(NOT ADATABASE_BACKEND STREQUAL "CUSTOM")
        message(FATAL_ERROR "ADATABASE_BACKEND must be FLASH25Q or CUSTOM")
    endif()
endif()

# ── func / aModbus (transport supplied by caller) ──────────────────────

# ── device / LED ──────────────────────────────────────────────────────
_aclass_requires(ADEV_LED_ENABLE ADRV_MODULE_GPIO_ENABLE)

# ── device / USART ────────────────────────────────────────────────────
foreach(feature INTERRUPT DIRECT ASYNC RS485 STATIC DYNAMIC)
    _aclass_requires(ADEV_USART_${feature}_ENABLE ADEV_USART_ENABLE)
endforeach()
_aclass_requires(ADEV_USART_ENABLE ADRV_MODULE_USART_ENABLE)
if(ADEV_USART_ENABLE AND NOT ADEV_USART_STATIC_ENABLE
   AND NOT ADEV_USART_DYNAMIC_ENABLE)
    message(FATAL_ERROR "USART requires STATIC or DYNAMIC allocation enabled")
endif()
foreach(feature INTERRUPT RS485)
    _aclass_requires(ADEV_USART_${feature}_ENABLE ADRV_USART_INTERRUPT_ENABLE)
endforeach()
# Direct is a user-buffer contract, not a DMA requirement (polling is supported).
_aclass_requires(ADEV_USART_ASYNC_ENABLE
    ADRV_USART_DMA_ENABLE ADRV_USART_INTERRUPT_ENABLE
)
# Async currently uses DMA, but does not require synchronous Direct APIs.
_aclass_requires(ADEV_USART_RS485_ENABLE ADRV_MODULE_GPIO_ENABLE)

# ── device / Flash25Q ─────────────────────────────────────────────────
_aclass_requires(ADEV_FLASH25Q_ENABLE ADRV_MODULE_QSPI_ENABLE)

# ── driver / GPIO ─────────────────────────────────────────────────────

# ── driver / USART ────────────────────────────────────────────────────
_aclass_requires(ADRV_MODULE_USART_ENABLE ADRV_MODULE_GPIO_ENABLE)
_aclass_requires(ADRV_USART_INTERRUPT_ENABLE ADRV_MODULE_USART_ENABLE)
_aclass_requires(ADRV_USART_DMA_ENABLE
    ADRV_MODULE_USART_ENABLE ADRV_MODULE_DMA_ENABLE
)

# ── driver / DMA ──────────────────────────────────────────────────────

# ── driver / SPI ──────────────────────────────────────────────────────
_aclass_requires(ADRV_MODULE_SPI_ENABLE ADRV_MODULE_GPIO_ENABLE)

# ── driver / QSPI ─────────────────────────────────────────────────────
_aclass_requires(ADRV_MODULE_QSPI_ENABLE ADRV_MODULE_GPIO_ENABLE)
