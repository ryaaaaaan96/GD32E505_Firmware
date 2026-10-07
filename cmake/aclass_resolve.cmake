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
foreach(module AOS_WORKQUEUE ABUS ABUS_STATIC ABUS_DYNAMIC ABUS_DEF_CHECK ASHELL
        ALOG AMEMORY
        ADATABASE ADATABASE_STATIC ADATABASE_DYNAMIC
        AMODBUS AMODBUS_STATIC AMODBUS_DYNAMIC AMODBUS_CLIENT AMODBUS_SERVER
        ADEV_LED ADEV_USART
        ADEV_USART_INTERRUPT ADEV_USART_DIRECT ADEV_USART_ASYNC
        ADEV_USART_RS485 ADEV_USART_STATIC ADEV_USART_DYNAMIC ADEV_FLASH25Q
        ADEV_FLASH25Q_STATIC ADEV_FLASH25Q_DYNAMIC)
    _aclass_option(${module}_ENABLE)
endforeach()

if(ABUS_ENABLE AND NOT ABUS_STATIC_ENABLE AND NOT ABUS_DYNAMIC_ENABLE)
    message(FATAL_ERROR "aBus requires STATIC or DYNAMIC allocation enabled")
endif()

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

# ── func / aModbus ────────────────────────────────────────────────────
_aclass_requires(AMODBUS_ENABLE ABUS_ENABLE)
if(AMODBUS_ENABLE AND NOT AMODBUS_STATIC_ENABLE AND
   NOT AMODBUS_DYNAMIC_ENABLE)
    message(FATAL_ERROR "aModbus requires STATIC or DYNAMIC allocation")
endif()
if(AMODBUS_ENABLE AND NOT AMODBUS_CLIENT_ENABLE AND
   NOT AMODBUS_SERVER_ENABLE)
    message(FATAL_ERROR "aModbus requires CLIENT or SERVER role")
endif()

# ── func / aLog ───────────────────────────────────────────────────────
# 日志后端由应用注入，不强制依赖 Shell 或数据库。
if(NOT "${ALOG_OUTPUT_LEVEL}" MATCHES "^[1-5]$")
    message(FATAL_ERROR "ALOG_OUTPUT_LEVEL must be an integer in [1, 5]")
endif()
if(NOT "${ALOG_LINE_BUFFER_SIZE}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "ALOG_LINE_BUFFER_SIZE must be in [256, 4096]")
endif()
if(ALOG_LINE_BUFFER_SIZE LESS 256 OR ALOG_LINE_BUFFER_SIZE GREATER 4096)
    message(FATAL_ERROR "ALOG_LINE_BUFFER_SIZE must be in [256, 4096]")
endif()

# aMemory 独立于具体设备，数据库只依赖统一存储接口。
_aclass_requires(ADATABASE_ENABLE AMEMORY_ENABLE)
if(NOT "${AMEMORY_MAX_PARTITIONS}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "AMEMORY_MAX_PARTITIONS must be in [1, 256]")
endif()
if(AMEMORY_MAX_PARTITIONS GREATER 256)
    message(FATAL_ERROR "AMEMORY_MAX_PARTITIONS must be in [1, 256]")
endif()

# ── func / aDataBase ──────────────────────────────────────────────────
if(ADATABASE_ENABLE)
    if(NOT ADATABASE_STATIC_ENABLE AND NOT ADATABASE_DYNAMIC_ENABLE)
        message(FATAL_ERROR "aDataBase requires STATIC or DYNAMIC allocation")
    endif()
endif()


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
_aclass_requires(ADEV_FLASH25Q_ENABLE ADRV_MODULE_SPI_ENABLE)
if(ADEV_FLASH25Q_ENABLE AND NOT ADEV_FLASH25Q_STATIC_ENABLE
   AND NOT ADEV_FLASH25Q_DYNAMIC_ENABLE)
    message(FATAL_ERROR "Flash25Q requires STATIC or DYNAMIC allocation")
endif()

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
