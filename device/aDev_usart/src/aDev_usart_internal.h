#ifndef ADEV_USART_INTERNAL_H
#define ADEV_USART_INTERNAL_H

#include "aDev_usart.h"
#include "aOS.h"

#include <stdatomic.h>
#define ADEV_USART_NEEDS_IRQ (ADEV_USART_HAS_INTERRUPT || ADEV_USART_HAS_DMA || ADEV_USART_HAS_RS485)

/** @brief TX 方向当前所有权；应用不得直接修改。 */
typedef enum {
    ADEV_USART_TX_IDLE,
    ADEV_USART_TX_STREAM,
    ADEV_USART_TX_DIRECT,
    ADEV_USART_TX_ASYNC,
} aDevUsartTxState_t;

/** @brief RX 方向当前所有权；应用不得直接修改。 */
typedef enum {
    ADEV_USART_RX_IDLE,
    ADEV_USART_RX_STREAM,
    ADEV_USART_RX_DIRECT,
} aDevUsartRxState_t;

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
    uint8_t interrupt_priority;
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

aStatus_t aDevUsartTxModeInit(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config);

#if ADEV_USART_HAS_DMA || ADEV_USART_HAS_INTERRUPT
static inline aStatus_t wait_for_event(void *wait_object,
                                const aTimepoint_t *end)
{
    return aOSWaitObjectWait(
        wait_object, aTimepointRemaining(end, aOSGetUptimeMs()));
}

#endif

static inline aSSize_t fail_with_wait_status(aStatus_t status,
                                      aTimeout_t timeout)
{
    return ((status == A_STATUS_BUSY) ||
            (status == A_STATUS_TIMEOUT))
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
}


#if ADEV_USART_HAS_DMA
/* TC wakes TX and DMA completion/error wakes RX. A bounded sleeping check
 * also detects TX DMA faults on drivers without a DMA-error IRQ callback. */
static inline aStatus_t direct_wait(aOSWaitObject_t object, const aTimepoint_t *end,
                             aBool_t check_tx_error)
{
    aTimeout_t remaining = aTimepointRemaining(end, aOSGetUptimeMs());
    if (aTimepointExpired(end, aOSGetUptimeMs())) return A_STATUS_TIMEOUT;
    if (check_tx_error && (remaining.type == A_TIMEOUT_TYPE_FOREVER ||
                          remaining.milliseconds > 10U))
        remaining = A_TIMEOUT_MS(10U);
    const aStatus_t status = aOSWaitObjectWait(object, remaining);
    if (status == A_STATUS_TIMEOUT && !aTimepointExpired(end, aOSGetUptimeMs()))
        return A_STATUS_OK;
    return status;
}

#endif

#endif
