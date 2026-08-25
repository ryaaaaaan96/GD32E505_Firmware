set(GD32_SPL "${CMAKE_CURRENT_SOURCE_DIR}/GD32E50x_standard_peripheral")
set(ADRV_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/port/${MCU_PORT}")
add_library(gd32e50x_spl STATIC
    "${GD32_SPL}/Source/gd32e50x_gpio.c" "${GD32_SPL}/Source/gd32e50x_rcu.c"
    "${GD32_SPL}/Source/gd32e50x_usart.c" "${GD32_SPL}/Source/gd32e50x_dma.c"
    "${GD32_SPL}/Source/gd32e50x_dbg.c" "${GD32_SPL}/Source/gd32e50x_spi.c"
    "${GD32_SPL}/Source/gd32e50x_sqpi.c")
target_include_directories(gd32e50x_spl PUBLIC
    "${GD32_SPL}/Include" "${ADRV_PORT_DIR}")
target_compile_definitions(gd32e50x_spl PRIVATE GD32E50X_SPL_PERIPHERALS)
target_link_libraries(gd32e50x_spl PUBLIC mcu_support)
target_compile_options(gd32e50x_spl PRIVATE -include gd32e50x_dbg.h -Wno-unused-variable)
add_library(aDrv STATIC
    src/aDrv.c src/aDrv_basic.c
    "${ADRV_PORT_DIR}/aDrv_gpio_${MCU_PORT}.c"
    "${ADRV_PORT_DIR}/aDrv_usart_${MCU_PORT}.c"
    "${ADRV_PORT_DIR}/aDrv_dma_${MCU_PORT}.c"
    "${ADRV_PORT_DIR}/aDrv_spi_${MCU_PORT}.c"
    "${ADRV_PORT_DIR}/aDrv_qspi_${MCU_PORT}.c")
target_include_directories(aDrv PUBLIC include PRIVATE "${ADRV_PORT_DIR}")
target_compile_definitions(aDrv PRIVATE
    GD32E50X_SPL_PERIPHERALS
    ADRV_MCU_NAME="${MCU_DEVICE}"
    ADRV_CAP_GPIO_PORT_COUNT=${MCU_GPIO_PORT_COUNT}
    ADRV_CAP_USART_COUNT=${MCU_USART_COUNT}
    ADRV_CAP_ADC_COUNT=${MCU_ADC_COUNT}
    ADRV_CAP_SPI_COUNT=${MCU_SPI_COUNT}
    ADRV_CAP_DMA_CHANNEL_COUNT=${MCU_DMA_CHANNEL_COUNT}
    ADRV_CAP_QSPI_COUNT=${MCU_QSPI_COUNT}
)
target_link_libraries(aDrv PUBLIC aLib aCore PRIVATE gd32e50x_spl)
