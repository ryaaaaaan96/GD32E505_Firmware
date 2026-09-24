#ifndef ADEV_USART_INTERNAL_H
#define ADEV_USART_INTERNAL_H

#include "aDev_usart.h"

struct aDevUsartHandle {
    aDrvUsartHandle_t drv_handle;
    aDevUsartRS485Config_t rs485;
    aDrvGpioHandle_t de_gpio;
    aDrvGpioHandle_t re_gpio;
    volatile aBool_t rs485_transmitting;
    aDevUsartMode_t mode;
    uint8_t *rx_buffer;
    size_t rx_buffer_size;
    size_t rx_dma_observed;
    volatile size_t rx_head;
    volatile size_t rx_tail;
    volatile size_t rx_count;
    uint8_t *tx_buffer;
    size_t tx_buffer_size;
    volatile size_t tx_head;
    volatile size_t tx_tail;
    volatile size_t tx_count;
    volatile size_t tx_dma_active;
    volatile aDevUsartTxState_t tx_state;
    volatile aDevUsartRxState_t rx_state;
    void *rx_mutex;
    void *tx_mutex;
    void *rx_wait_object;
    void *tx_wait_object;
    aDevUsartEventCallback_t event_callback;
    void *event_argument;
    volatile uint32_t idle_event_count;
    volatile aBool_t rx_overflow;
    volatile aStatus_t rx_error;
    volatile aStatus_t tx_error;
    aBool_t dynamic_storage;
};

_Static_assert(sizeof(struct aDevUsartHandle) <=
                   ADEV_USART_STATIC_STORAGE_SIZE,
               "Increase ADEV_USART_STATIC_STORAGE_SIZE");

/* Begin/Complete 仅由 USART ISR 或屏蔽 USART IRQ 的 TX 路径调用。 */
aStatus_t aDevUsartRS485Init(aDevUsartHandle_t *handle,
                           const aDevUsartRS485Config_t *config);
aStatus_t aDevUsartRS485DeInit(aDevUsartHandle_t *handle);
aStatus_t aDevUsartRS485Begin(aDevUsartHandle_t *handle);
aStatus_t aDevUsartRS485Complete(aDevUsartHandle_t *handle);

void aDevUsartNotifyEvent(aDevUsartHandle_t *handle,
                          aDevUsartEvent_t event);
aStatus_t aDevUsartRegisterIrqCallback(
    aDevUsartHandle_t *handle, aDrvUsartExti_t trigger,
    aDrvInterruptCallback_t callback, uint8_t priority, aBool_t enabled);
aStatus_t aDevUsartDmaRxCommit(aDevUsartHandle_t *handle);
aStatus_t aDevUsartRxModeInit(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config);
void aDevUsartRs485ArmComplete(aDevUsartHandle_t *handle);

#endif
