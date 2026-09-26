# Central resolver: validate requests; never silently enable dependencies.
# First validate all inputs, then check dependencies by module.
# Application-specific requirements belong to app/CMakeLists.txt or its children.
function(_aclass_option input output)
    if(NOT DEFINED ${input})
        message(FATAL_ERROR "Missing configuration input: ${input} (config/aclass_config.cmake)")
    endif()
    if(NOT "${${input}}" MATCHES "^(ON|OFF|TRUE|FALSE|0|1)$")
        message(FATAL_ERROR "${input} must be boolean, got '${${input}}'")
    endif()
    if(${input})
        set(${output} ON PARENT_SCOPE)
    else()
        set(${output} OFF PARENT_SCOPE)
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
foreach(module ASHELL ADATABASE AMODBUS ADEV_LED ADEV_USART
        ADEV_USART_INTERRUPT ADEV_USART_DMA ADEV_USART_ASYNC
        ADEV_USART_RS485 ADEV_FLASH25Q)
    _aclass_option(${module}_REQUESTED ${module}_ENABLED)
endforeach()

foreach(module GPIO USART DMA SPI QSPI)
    _aclass_option(ADRV_MODULE_${module}_REQUESTED ADRV_MODULE_${module})
endforeach()
foreach(feature INTERRUPT ASYNC)
    _aclass_option(ADRV_USART_${feature}_REQUESTED ADRV_USART_${feature})
endforeach()

# ── func / aShell ─────────────────────────────────────────────────────

# ── func / aDataBase ──────────────────────────────────────────────────
_aclass_requires(ADATABASE_REQUESTED ADEV_FLASH25Q_REQUESTED)

# ── func / aModbus (transport supplied by caller) ──────────────────────

# ── device / LED ──────────────────────────────────────────────────────
_aclass_requires(ADEV_LED_REQUESTED ADRV_MODULE_GPIO_REQUESTED)

# ── device / USART ────────────────────────────────────────────────────
foreach(feature INTERRUPT DMA ASYNC RS485)
    _aclass_requires(ADEV_USART_${feature}_REQUESTED ADEV_USART_REQUESTED)
endforeach()
_aclass_requires(ADEV_USART_REQUESTED ADRV_MODULE_USART_REQUESTED)
foreach(feature INTERRUPT RS485)
    _aclass_requires(ADEV_USART_${feature}_REQUESTED ADRV_USART_INTERRUPT_REQUESTED)
endforeach()
_aclass_requires(ADEV_USART_DMA_REQUESTED
    ADRV_USART_ASYNC_REQUESTED ADRV_USART_INTERRUPT_REQUESTED
)
_aclass_requires(ADEV_USART_ASYNC_REQUESTED ADEV_USART_DMA_REQUESTED)
_aclass_requires(ADEV_USART_RS485_REQUESTED ADRV_MODULE_GPIO_REQUESTED)

# ── device / Flash25Q ─────────────────────────────────────────────────
_aclass_requires(ADEV_FLASH25Q_REQUESTED ADRV_MODULE_QSPI_REQUESTED)

# ── driver / GPIO ─────────────────────────────────────────────────────

# ── driver / USART ────────────────────────────────────────────────────
_aclass_requires(ADRV_MODULE_USART_REQUESTED ADRV_MODULE_GPIO_REQUESTED)
_aclass_requires(ADRV_USART_INTERRUPT_REQUESTED ADRV_MODULE_USART_REQUESTED)
_aclass_requires(ADRV_USART_ASYNC_REQUESTED
    ADRV_MODULE_USART_REQUESTED ADRV_MODULE_DMA_REQUESTED
)

# ── driver / DMA ──────────────────────────────────────────────────────

# ── driver / SPI ──────────────────────────────────────────────────────
_aclass_requires(ADRV_MODULE_SPI_REQUESTED ADRV_MODULE_GPIO_REQUESTED)

# ── driver / QSPI ─────────────────────────────────────────────────────
_aclass_requires(ADRV_MODULE_QSPI_REQUESTED ADRV_MODULE_GPIO_REQUESTED)
