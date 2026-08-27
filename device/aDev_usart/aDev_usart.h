#ifndef ADEV_USART_H
#define ADEV_USART_H

#include "aDrv_usart.h"
#include "aLib.h"

typedef enum {
    ADEV_USART_MODE_POLLING,
    ADEV_USART_MODE_DMA_TX,
    ADEV_USART_MODE_INTERRUPT_IDLE,
} aDevUsartMode_t;

typedef struct {
    aDrvUsartConfig_t drv_config;
    aDevUsartMode_t mode;
    uint8_t interrupt_priority;
    uint8_t *rx_buffer;
    size_t rx_buffer_size;
    uint8_t *tx_buffer;
    size_t tx_buffer_size;
} aDevUsartConfig_t;

typedef struct {
    aDrvUsartHandle_t drv_handle;
    aDevUsartMode_t mode;
    uint8_t *rx_buffer;
    size_t rx_buffer_size;
    volatile size_t rx_head;
    volatile size_t rx_tail;
    volatile size_t rx_count;
    uint8_t *tx_buffer;
    size_t tx_buffer_size;
    volatile size_t tx_head;
    volatile size_t tx_tail;
    volatile size_t tx_count;
    volatile uint32_t idle_event_count;
    volatile uint8_t rx_overflow;
} aDevUsartHandle_t;

void aDevUsartConfigStructInit(aDevUsartConfig_t *config);
void aDevUsartHandleStructInit(aDevUsartHandle_t *handle);
aStatus_t aDevUsartInit(const aDevUsartConfig_t *config,
                        aDevUsartHandle_t *handle);
aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle);
aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *buffer,
                       size_t buffer_size, aTimeout_t timeout);
aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *data,
                        size_t data_size, aTimeout_t timeout);
aStatus_t aDevUsartWaitTransmitComplete(aDevUsartHandle_t *handle,
                                         aTimeout_t timeout);
uint32_t aDevUsartGetIdleEventCount(const aDevUsartHandle_t *handle);
bool aDevUsartHasRxOverflowed(const aDevUsartHandle_t *handle);
void aDevUsartClearRxOverflow(aDevUsartHandle_t *handle);

#endif
