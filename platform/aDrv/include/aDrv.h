#ifndef ADRV_H
#define ADRV_H
#include "aStatus.h"
#include <stddef.h>
#include <stdint.h>
typedef void (*aDrvInterruptCallback_t)(void *argument);
typedef struct {
    const char *mcu_name; uint32_t core_clock_hz;
    uint8_t gpio_port_count, usart_count, adc_count, spi_count;
    uint8_t dma_channel_count, qspi_count;
} aDrvCapabilities_t;
aStatus_t aDrvInit(void);
const aDrvCapabilities_t *aDrvGetCapabilities(void);
uint32_t aDrvGetTickMs(void);
void aDrvDelayMs(uint32_t milliseconds);
#endif
