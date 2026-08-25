#include "aDrv.h"
#include "aCore.h"
static aDrvCapabilities_t s_capabilities = {
    .mcu_name = ADRV_MCU_NAME,
    .core_clock_hz = 0U,
    .gpio_port_count = ADRV_CAP_GPIO_PORT_COUNT,
    .usart_count = ADRV_CAP_USART_COUNT,
    .adc_count = ADRV_CAP_ADC_COUNT,
    .spi_count = ADRV_CAP_SPI_COUNT,
    .dma_channel_count = ADRV_CAP_DMA_CHANNEL_COUNT,
    .qspi_count = ADRV_CAP_QSPI_COUNT
};
aStatus_t aDrvInit(void)
{ aCoreInit(); s_capabilities.core_clock_hz = aCoreClockHz(); return A_STATUS_OK; }
const aDrvCapabilities_t *aDrvGetCapabilities(void) { return &s_capabilities; }
uint32_t aDrvGetTickMs(void) { return aCoreMillis(); }
void aDrvDelayMs(uint32_t milliseconds) { aCoreDelayMs(milliseconds); }
