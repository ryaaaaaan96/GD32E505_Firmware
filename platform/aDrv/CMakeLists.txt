include("${CMAKE_CURRENT_SOURCE_DIR}/config/aDrv_defaults.cmake")

set(
    ADRV_CONFIG
    "${CMAKE_SOURCE_DIR}/config/aDrv_config.cmake"
    CACHE FILEPATH
    "aDrv module configuration"
)

if(NOT EXISTS "${ADRV_CONFIG}")
    message(FATAL_ERROR "aDrv configuration does not exist: ${ADRV_CONFIG}")
endif()

include("${ADRV_CONFIG}")

if(ADRV_USART_INTERRUPT AND NOT ADRV_MODULE_USART)
    message(FATAL_ERROR
        "ADRV_USART_INTERRUPT requires ADRV_MODULE_USART"
    )
endif()

if(ADRV_USART_ASYNC AND NOT ADRV_MODULE_USART)
    message(FATAL_ERROR
        "ADRV_USART_ASYNC requires ADRV_MODULE_USART"
    )
endif()

# Like Zephyr's UART_ASYNC_API, asynchronous USART support selects DMA.
if(ADRV_USART_ASYNC)
    set(ADRV_MODULE_DMA 1 CACHE BOOL "Enable the aDrv DMA module" FORCE)
endif()

if(NOT DEFINED BOARD_HXTAL_HZ OR BOARD_HXTAL_HZ LESS_EQUAL 0)
    message(FATAL_ERROR "BOARD_HXTAL_HZ must define the board HXTAL frequency")
endif()

# These adapters use the shared GPIO pin resolver.
foreach(module USART SPI QSPI)
    if(ADRV_MODULE_${module} AND NOT ADRV_MODULE_GPIO)
        message(FATAL_ERROR
            "ADRV_MODULE_${module} requires ADRV_MODULE_GPIO"
        )
    endif()
endforeach()

set(GD32_DEVICE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/CMSIS/Device/GD/GD32E50x"
)

set(GD32_SPL_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/GD32E50x_standard_peripheral"
)

set(GD32_CONFIG_DIR
    "${CMAKE_BINARY_DIR}/generated/aDrv"
)

file(MAKE_DIRECTORY
    "${GD32_CONFIG_DIR}"
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config/gd32e50x_libopt.h.in"
    "${GD32_CONFIG_DIR}/gd32e50x_libopt.h"
    @ONLY
)

# SystemInit requires MISC. Other SPL sources follow the enabled aDrv modules.
set(GD32_VENDOR_SOURCES
    "${GD32_DEVICE_DIR}/Source/system_gd32e50x.c"
    "${GD32_SPL_DIR}/Source/gd32e50x_misc.c"
)
set(ADRV_SOURCES
    src/aDrv.c
    src/aDrv_basic.c
)
set(GD32_RCU_DRIVER_REQUIRED OFF)

if(TOOLCHAIN_NAME STREQUAL "GCC")
    if(NOT MCU_STARTUP_VARIANT)
        message(FATAL_ERROR
            "MCU profile must define MCU_STARTUP_VARIANT for GCC"
        )
    endif()

    set(GD32_STARTUP_SOURCE
        "${GD32_DEVICE_DIR}/Source/GCC/startup_${MCU_STARTUP_VARIANT}.S"
    )
    if(NOT EXISTS "${GD32_STARTUP_SOURCE}")
        message(FATAL_ERROR
            "GD32 GCC startup does not exist: ${GD32_STARTUP_SOURCE}"
        )
    endif()

    list(APPEND ADRV_SOURCES
        "${GD32_STARTUP_SOURCE}"
    )
endif()

if(ADRV_MODULE_GPIO)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_gpio.c"
    )
    list(APPEND ADRV_SOURCES
        src/aDrv_gpio.c
    )
    set(GD32_RCU_DRIVER_REQUIRED ON)
endif()

if(ADRV_MODULE_USART)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_usart.c"
    )
    list(APPEND ADRV_SOURCES
        src/aDrv_usart.c
    )

    if(ADRV_USART_INTERRUPT)
        list(APPEND ADRV_SOURCES
            src/aDrv_usart_irq.c
        )
    else()
        list(APPEND ADRV_SOURCES
            src/aDrv_usart_irq_stub.c
        )
    endif()

    if(ADRV_USART_ASYNC)
        list(APPEND ADRV_SOURCES
            src/aDrv_usart_async.c
        )
    else()
        list(APPEND ADRV_SOURCES
            src/aDrv_usart_async_stub.c
        )
    endif()

    set(GD32_RCU_DRIVER_REQUIRED ON)
endif()

if(ADRV_MODULE_DMA)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_dma.c"
    )
    list(APPEND ADRV_SOURCES
        src/aDrv_dma.c
    )
    set(GD32_RCU_DRIVER_REQUIRED ON)
endif()

if(ADRV_MODULE_SPI)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_spi.c"
    )
    list(APPEND ADRV_SOURCES
        src/aDrv_spi.c
    )
    set(GD32_RCU_DRIVER_REQUIRED ON)
endif()

if(ADRV_MODULE_QSPI)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_sqpi.c"
    )
    list(APPEND ADRV_SOURCES
        src/aDrv_qspi.c
    )
    set(GD32_RCU_DRIVER_REQUIRED ON)
endif()

if(GD32_RCU_DRIVER_REQUIRED)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_rcu.c"
    )
endif()

add_library(
    aDrv_gd32e505_vendor
    STATIC
        ${GD32_VENDOR_SOURCES}
)

target_include_directories(
    aDrv_gd32e505_vendor
    PUBLIC
        "${GD32_CONFIG_DIR}"
        "${GD32_DEVICE_DIR}/Include"
        "${GD32_SPL_DIR}/Include"
)

target_compile_definitions(
    aDrv_gd32e505_vendor
    PUBLIC
        ${MCU_PUBLIC_DEFINITIONS}
        HXTAL_VALUE=${BOARD_HXTAL_HZ}U
)

if(BOARD_HXTAL_BYPASS)
    target_compile_definitions(
        aDrv_gd32e505_vendor
        PRIVATE
            GD32_HXTAL_BYPASS=1
    )
endif()

target_compile_options(
    aDrv_gd32e505_vendor
    PRIVATE
        -Wno-unused-variable
)

target_link_libraries(
    aDrv_gd32e505_vendor
    PUBLIC
        aCore
)

add_library(
    aDrv
    STATIC
        ${ADRV_SOURCES}
)

target_include_directories(
    aDrv
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(
    aDrv
    PUBLIC
        aLib
        aCore
    PRIVATE
        aDrv_gd32e505_vendor
)

message(
    STATUS
    "aDrv modules: GPIO=${ADRV_MODULE_GPIO}, USART=${ADRV_MODULE_USART}, "
    "DMA=${ADRV_MODULE_DMA}, SPI=${ADRV_MODULE_SPI}, QSPI=${ADRV_MODULE_QSPI}"
)

message(
    STATUS
    "aDrv USART capabilities: interrupt=${ADRV_USART_INTERRUPT}, "
    "async=${ADRV_USART_ASYNC}"
)

message(
    STATUS
    "Board HXTAL: ${BOARD_HXTAL_HZ} Hz, bypass=${BOARD_HXTAL_BYPASS}"
)
