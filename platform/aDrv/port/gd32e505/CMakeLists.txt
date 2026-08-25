set(GD32_DEVICE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/CMSIS/Device/GD/GD32E50x")
set(GD32_SPL_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/GD32E50x_standard_peripheral")
set(GD32_CONFIG_DIR
    "${CMAKE_BINARY_DIR}/generated/aDrv/${MCU_PORT}")

# USART, SPI and QSPI adapters share the GPIO pin resolver and therefore
# require the public GPIO module to be enabled as well.
foreach(module USART SPI QSPI)
    if(ADRV_MODULE_${module} AND NOT ADRV_MODULE_GPIO)
        message(FATAL_ERROR
            "ADRV_MODULE_${module} requires ADRV_MODULE_GPIO")
    endif()
endforeach()

file(MAKE_DIRECTORY "${GD32_CONFIG_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config/gd32e50x_libopt.h.in"
    "${GD32_CONFIG_DIR}/gd32e50x_libopt.h"
    @ONLY
)

# SystemInit always requires MISC.  Other SPL sources and their matching aDrv
# adapters are selected from the same ADRV_MODULE_* values that generated
# gd32e50x_libopt.h above.
set(GD32_VENDOR_SOURCES
    "${GD32_DEVICE_DIR}/Source/system_gd32e50x.c"
    "${GD32_SPL_DIR}/Source/gd32e50x_misc.c"
)
set(GD32_PORT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/aDrv_basic_gd32e505.c"
)
set(GD32_RCU_DRIVER_REQUIRED OFF)

if(ADRV_MODULE_GPIO)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_gpio.c")
    list(APPEND GD32_PORT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/aDrv_gpio_gd32e505.c")
    set(GD32_RCU_DRIVER_REQUIRED ON)
endif()

if(ADRV_MODULE_USART)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_usart.c")
    list(APPEND GD32_PORT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/aDrv_usart_gd32e505.c")
    set(GD32_RCU_DRIVER_REQUIRED ON)
endif()

if(ADRV_MODULE_DMA)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_dma.c")
    list(APPEND GD32_PORT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/aDrv_dma_gd32e505.c")
    set(GD32_RCU_DRIVER_REQUIRED ON)
endif()

if(ADRV_MODULE_SPI)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_spi.c")
    list(APPEND GD32_PORT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/aDrv_spi_gd32e505.c")
    set(GD32_RCU_DRIVER_REQUIRED ON)
endif()

if(ADRV_MODULE_QSPI)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_sqpi.c")
    list(APPEND GD32_PORT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/aDrv_qspi_gd32e505.c")
    set(GD32_RCU_DRIVER_REQUIRED ON)
endif()

if(GD32_RCU_DRIVER_REQUIRED)
    list(APPEND GD32_VENDOR_SOURCES
        "${GD32_SPL_DIR}/Source/gd32e50x_rcu.c")
endif()

add_library(aDrv_gd32e505_vendor STATIC ${GD32_VENDOR_SOURCES})

target_include_directories(aDrv_gd32e505_vendor PUBLIC
    "${GD32_CONFIG_DIR}"
    "${GD32_DEVICE_DIR}/Include"
    "${GD32_SPL_DIR}/Include"
)

target_compile_definitions(aDrv_gd32e505_vendor PUBLIC
    ${MCU_PUBLIC_DEFINITIONS}
)

target_compile_options(aDrv_gd32e505_vendor PRIVATE
    -Wno-unused-variable
)

target_link_libraries(aDrv_gd32e505_vendor PUBLIC aCore)

message(STATUS
    "aDrv modules: GPIO=${ADRV_MODULE_GPIO}, USART=${ADRV_MODULE_USART}, "
    "DMA=${ADRV_MODULE_DMA}, SPI=${ADRV_MODULE_SPI}, QSPI=${ADRV_MODULE_QSPI}")

set(ADRV_PORT_SOURCES ${GD32_PORT_SOURCES} PARENT_SCOPE)
set(ADRV_PORT_VENDOR_TARGET aDrv_gd32e505_vendor PARENT_SCOPE)
