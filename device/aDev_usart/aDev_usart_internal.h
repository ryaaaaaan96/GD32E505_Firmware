#ifndef ADEV_USART_INTERNAL_H
#define ADEV_USART_INTERNAL_H

#include "aDev_usart.h"
#include "aOS.h"

#include <stdatomic.h>
#define ADEV_USART_NEEDS_IRQ (ADEV_USART_HAS_INTERRUPT || ADEV_USART_HAS_DMA || ADEV_USART_HAS_RS485)

/* Private ownership bridge within aDevUsart. */
aStatus_t aDevUsartTxQueueClaim(aDevUsartHandle_t *handle,
                                const void *owner);
aStatus_t aDevUsartTxQueueRelease(aDevUsartHandle_t *handle,
                                  const void *owner);
aStatus_t aDevUsartWriteAsyncQueued(aDevUsartHandle_t *handle,
                                    const void *owner,
                                    const aDevUsartWriteRequest_t *request);
aStatus_t aDevUsartWriteAsyncCancelQueued(aDevUsartHandle_t *handle,
                                          const void *owner);

typedef struct aDevUsartReadNode aDevUsartReadNode_t;

struct aDevUsartReadNode {
    aDevUsartReadNode_t *next;
    aDevUsartReadToken_t token;
    aDevUsartReadRequest_t request;
    aTimepoint_t deadline;
    aDevUsartRxEvent_t event;
    uint8_t snapshot[64];
};

struct aDevUsartHandle {
    aDrvUsartHandle_t drv_handle;
    aDevUsartRS485Config_t rs485;
    aDrvGpioHandle_t de_gpio;
    aDrvGpioHandle_t re_gpio;
    volatile aBool_t rs485_transmitting;
    aDevUsartMode_t mode;
    uint8_t *rx_buffer;
    size_t rx_buffer_size;
    volatile size_t rx_head;
    volatile size_t rx_tail;
    volatile size_t rx_count;
    volatile size_t rx_dma_produced;
    volatile size_t rx_dma_consumed;
    volatile aBool_t rx_dma_active;
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
    aOSMutex_t event_mutex;
    aOSWorkItem_t event_work;
    aOSWorkItem_t tx_completion_work;
    aOSWorkItem_t rx_completion_work;
    aOSTimer_t tx_deadline_timer;
    atomic_uint_least32_t pending_events;
    atomic_bool tx_completion_claimed;
    aDevUsartTxEvent_t tx_completion_event;
    aDevUsartTxCallback_t tx_callback;
    void *tx_callback_argument;
    const void *tx_queue_owner;
    const void *tx_async_buffer;
    size_t tx_async_size;
    aStatus_t tx_async_status;
    aDevUsartReadNode_t *rx_request_head;
    aDevUsartReadNode_t *rx_request_tail;
    aDevUsartReadNode_t *rx_complete_head;
    aDevUsartReadNode_t *rx_complete_tail;
    aDevUsartReadToken_t rx_next_token;
    aOSTimer_t rx_deadline_timer;
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
void aDevUsartEventWork(void *argument);
void aDevUsartAsyncTxWork(void *argument);
void aDevUsartAsyncRxWork(void *argument);
void aDevUsartRxDmaComplete(void *argument);
void aDevUsartRxDmaNotifyFromISR(aDevUsartHandle_t *handle);
void aDevUsartAsyncTxTimeout(void *argument);
aStatus_t aDevUsartRegisterIrqCallback(
    aDevUsartHandle_t *handle, aDrvUsartExti_t trigger,
    aDrvInterruptCallback_t callback, uint8_t priority, aBool_t enabled);
aStatus_t aDevUsartRxModeInit(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config);
aStatus_t aDevUsartDmaRxRefresh(aDevUsartHandle_t *handle);
aStatus_t aDevUsartDmaRxCopy(aDevUsartHandle_t *handle, void *buffer,
                             size_t capacity, size_t *copied);
void aDevUsartRs485ArmComplete(aDevUsartHandle_t *handle);

#endif
