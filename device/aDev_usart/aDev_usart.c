#include "aDev_usart.h"
#include "aDev_usart_internal.h"

#include "aOS.h"

#include <limits.h>
#include <string.h>

#if ADEV_USART_HAS_DMA
static aStatus_t dma_tx_start_locked(aDevUsartHandle_t *handle);
#endif
static void rs485_complete(aDevUsartHandle_t *handle);
#if ADEV_USART_HAS_ASYNC
static void async_tx_complete(aDevUsartHandle_t *handle, aStatus_t status,
                              size_t transferred, aBool_t from_isr)
{
    if (atomic_exchange_explicit(&handle->tx_completion_claimed, A_TRUE,
                                 memory_order_acq_rel)) {
        return;
    }
    handle->tx_completion_event.buffer = handle->tx_async_buffer;
    handle->tx_completion_event.requested = handle->tx_async_size;
    handle->tx_completion_event.transferred = transferred;
    handle->tx_completion_event.status = status;
    handle->tx_async_status = status;
    handle->tx_dma_active = 0U;
    handle->tx_state = handle->tx_queue_owner != NULL
                           ? ADEV_USART_TX_QUEUE : ADEV_USART_TX_IDLE;
    if (from_isr) {
        (void)aOSWorkSubmitFromISR(&handle->tx_completion_work,
                                   aDevUsartAsyncTxWork, handle);
    } else {
        (void)aOSWorkSubmit(&handle->tx_completion_work,
                            aDevUsartAsyncTxWork, handle);
    }
}

void aDevUsartAsyncTxWork(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    const aDevUsartTxEvent_t event = handle->tx_completion_event;
    aDevUsartTxCallback_t callback = handle->tx_callback;
    void *callback_argument = handle->tx_callback_argument;

    handle->tx_callback = NULL;
    handle->tx_callback_argument = NULL;
    handle->tx_async_buffer = NULL;
    handle->tx_async_size = 0U;
    if (callback != NULL) {
        callback(handle, &event, callback_argument);
    }
}

void aDevUsartAsyncTxTimeout(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    size_t remaining = handle->tx_async_size;
    size_t transferred = 0U;

    if (aOSMutexLock(handle->tx_mutex, A_TIMEOUT_FOREVER) != A_STATUS_OK) {
        return;
    }
    if (handle->tx_state != ADEV_USART_TX_ASYNC) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return;
    }
    if (aDrvUsartAsyncTxGetRemaining(&handle->drv_handle, &remaining) ==
        A_STATUS_OK) {
        transferred = handle->tx_async_size - remaining;
    }
    (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
    if (!handle->rs485.enabled) {
        (void)aDrvUsartSetInterruptEnabled(&handle->drv_handle,
                                           ADRV_USART_EXTI_TC, A_FALSE);
    }
    async_tx_complete(handle, A_STATUS_TIMEOUT, transferred, A_FALSE);
    (void)aOSMutexUnlock(handle->tx_mutex);
}

#endif

/* 调用者已屏蔽 USART IRQ，或正在 USART ISR 内。 */
static void rs485_complete(aDevUsartHandle_t *handle)
{
#if ADEV_USART_HAS_RS485
    const aStatus_t status = aDevUsartRS485Complete(handle);
    if (status != A_STATUS_OK) {
        handle->tx_error = status;
    }
#else
    (void)handle;
#endif
}

#if ADEV_USART_HAS_RS485
void aDevUsartRs485ArmComplete(aDevUsartHandle_t *handle)
{
    if (handle->rs485_transmitting) {
        const aStatus_t status = aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
        if (status != A_STATUS_OK) {
            handle->tx_error = status;
        }
    }
}

#endif

void aDevUsartEventWork(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    for (;;) {
        uint32_t events = atomic_exchange_explicit(
            &handle->pending_events, 0U, memory_order_acquire);
        aDevUsartEventCallback_t callback;
        void *callback_argument;

        if (events == 0U) return;
        if (aOSMutexLock(handle->event_mutex, A_TIMEOUT_FOREVER) !=
            A_STATUS_OK) {
            continue;
        }
        callback = handle->event_callback;
        callback_argument = handle->event_argument;
        (void)aOSMutexUnlock(handle->event_mutex);
        if (callback == NULL) continue;

        for (uint32_t event = 0U; event <= ADEV_USART_EVENT_RX_ERROR;
             ++event) {
            if ((events & (1UL << event)) != 0U) {
                callback((aDevUsartEvent_t)event, callback_argument);
            }
        }
    }
}

void aDevUsartNotifyEvent(aDevUsartHandle_t *handle,
                          aDevUsartEvent_t event)
{
    const uint32_t event_bit = 1UL << (uint32_t)event;
    atomic_fetch_or_explicit(&handle->pending_events, event_bit,
                             memory_order_release);
    (void)aOSWorkSubmitFromISR(&handle->event_work,
                               aDevUsartEventWork, handle);
}

#if ADEV_USART_HAS_INTERRUPT
static void irq_transmit(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    if (handle->tx_count == 0U) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TXE, A_FALSE);
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
        return;
    }

    if (aDrvUsartTryWriteByte(&handle->drv_handle,
                              handle->tx_buffer[handle->tx_tail]) ==
        A_STATUS_OK) {
        handle->tx_tail = (handle->tx_tail + 1U) % handle->tx_buffer_size;
        --handle->tx_count;
        aOSWaitObjectNotifyFromISR(handle->tx_wait_object);
        aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_TX_SPACE);
        if (handle->tx_count == 0U) {
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TXE, A_FALSE);
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
        }
    }
}

#endif

#if ADEV_USART_NEEDS_IRQ
static void irq_transmit_complete(void *argument)
{
    aDevUsartHandle_t *handle = argument;

#if ADEV_USART_HAS_ASYNC
    if (handle->tx_state == ADEV_USART_TX_ASYNC) {
        size_t remaining = handle->tx_async_size;
        const aStatus_t status = aDrvUsartAsyncTxGetRemaining(
            &handle->drv_handle, &remaining);

        if ((status == A_STATUS_OK) && (remaining != 0U)) {
            return;
        }
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
        rs485_complete(handle);
        async_tx_complete(handle, status,
                          status == A_STATUS_OK
                              ? handle->tx_async_size - remaining : 0U,
                          A_TRUE);
        aOSWaitObjectNotifyFromISR(handle->tx_wait_object);
        return;
    }

#endif

#if ADEV_USART_HAS_DMA
    if ((handle->mode & ADEV_USART_TX_MASK) ==
        ADEV_USART_TX_DMA_BUFFERED) {
        size_t remaining = 0U;
        aStatus_t status;

        if (handle->tx_dma_active == 0U) {
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
            rs485_complete(handle);
            aOSWaitObjectNotifyFromISR(handle->tx_wait_object);
            aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_TX_COMPLETE);
            return;
        }

        status = aDrvUsartAsyncTxGetRemaining(
            &handle->drv_handle, &remaining);
        if ((status == A_STATUS_OK) && (remaining != 0U)) {
            return;
        }
        if (status != A_STATUS_OK) {
            handle->tx_error = status;
            (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
            handle->tx_dma_active = 0U;
        } else {
            handle->tx_tail =
                (handle->tx_tail + handle->tx_dma_active) %
                handle->tx_buffer_size;
            handle->tx_count -= handle->tx_dma_active;
            handle->tx_dma_active = 0U;
            status = dma_tx_start_locked(handle);
            if (status != A_STATUS_OK) {
                handle->tx_error = status;
            }
        }

        aOSWaitObjectNotifyFromISR(handle->tx_wait_object);
        aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_TX_SPACE);
        if ((handle->tx_count == 0U) ||
            (handle->tx_error != A_STATUS_OK)) {
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
            rs485_complete(handle);
            aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_TX_COMPLETE);
        }
        return;
    }

#endif

    (void)aDrvUsartSetInterruptEnabled(
        &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
    rs485_complete(handle);
    aOSWaitObjectNotifyFromISR(handle->tx_wait_object);
    aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_TX_COMPLETE);
}

aStatus_t aDevUsartRegisterIrqCallback(aDevUsartHandle_t *handle,
                                       aDrvUsartExti_t trigger,
                                       aDrvInterruptCallback_t callback,
                                       uint8_t priority, aBool_t enabled)
{
    aDrvUsartExtiConfig_t config;

    config.trigger = trigger;
    config.priority = priority;
    config.callback = callback;
    config.argument = handle;
    config.enabled = enabled;
    return aDrvUsartRegisterCallback(&handle->drv_handle, &config);
}

#endif

static aBool_t mode_is_valid(aDevUsartMode_t mode)
{
    const aDevUsartMode_t rx_mode = mode & ADEV_USART_RX_MASK;

    if ((mode & ~ADEV_USART_MODE_VALID_MASK) != 0U) {
        return A_FALSE;
    }
    if (((mode & ADEV_USART_OPTION_RX_IDLE) != 0U) &&
        (rx_mode == ADEV_USART_RX_POLLING)) {
        return A_FALSE;
    }

    switch (mode & ADEV_USART_TX_MASK) {
    case ADEV_USART_TX_POLLING:
    case ADEV_USART_TX_INTERRUPT_BUFFERED:
    case ADEV_USART_TX_DMA_BUFFERED:
        break;
    default:
        return A_FALSE;
    }

    switch (rx_mode) {
    case ADEV_USART_RX_POLLING:
    case ADEV_USART_RX_INTERRUPT_BUFFERED:
    case ADEV_USART_RX_DMA_BUFFERED:
        return A_TRUE;
    default:
        return A_FALSE;
    }
}

static aStatus_t tx_mode_init(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config)
{
    switch (config->mode & ADEV_USART_TX_MASK) {
    case ADEV_USART_TX_POLLING:
#if ADEV_USART_NEEDS_IRQ
        if (config->rs485.enabled || ADEV_USART_HAS_ASYNC)
            return aDevUsartRegisterIrqCallback(
                handle, ADRV_USART_EXTI_TC, irq_transmit_complete,
                config->interrupt_priority, A_FALSE);
#endif
        (void)handle;
        return A_STATUS_OK;
#if ADEV_USART_HAS_INTERRUPT
    case ADEV_USART_TX_INTERRUPT_BUFFERED: {
        aStatus_t status;

        if (!aDrvUsartInterruptIsSupported()) {
            return A_STATUS_UNSUPPORTED;
        }
        if ((config->tx_buffer == NULL) ||
            (config->tx_buffer_size < 2U)) {
            return A_STATUS_INVALID_PARAM;
        }
        handle->tx_buffer = config->tx_buffer;
        handle->tx_buffer_size = config->tx_buffer_size;
        status = aDevUsartRegisterIrqCallback(
            handle, ADRV_USART_EXTI_TXE, irq_transmit,
            config->interrupt_priority, A_FALSE);
        if (status != A_STATUS_OK) {
            return status;
        }
        return aDevUsartRegisterIrqCallback(
            handle, ADRV_USART_EXTI_TC, irq_transmit_complete,
            config->interrupt_priority, A_FALSE);
    }
#endif

#if ADEV_USART_HAS_DMA
    case ADEV_USART_TX_DMA_BUFFERED: {
        aStatus_t status;

        if (!aDrvUsartInterruptIsSupported() ||
            !aDrvUsartAsyncTxIsSupported(&handle->drv_handle)) {
            return A_STATUS_UNSUPPORTED;
        }
        if ((config->tx_buffer == NULL) ||
            (config->tx_buffer_size < 2U)) {
            return A_STATUS_INVALID_PARAM;
        }
        handle->tx_buffer = config->tx_buffer;
        handle->tx_buffer_size = config->tx_buffer_size;
        status = aDevUsartRegisterIrqCallback(
            handle, ADRV_USART_EXTI_TC, irq_transmit_complete,
            config->interrupt_priority, A_FALSE);
        return status;
    }
#endif

    default:
        return A_STATUS_INVALID_PARAM;
    }
}

static aStatus_t wait_objects_create(aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if (((handle->mode & ADEV_USART_TX_MASK) ==
         ADEV_USART_TX_INTERRUPT_BUFFERED) ||
        ((handle->mode & ADEV_USART_TX_MASK) ==
         ADEV_USART_TX_DMA_BUFFERED)) {
        status = aOSWaitObjectCreate(&handle->tx_wait_object);
        if (status != A_STATUS_OK) {
            return status;
        }
    }

    if ((handle->mode & ADEV_USART_RX_MASK) ==
            ADEV_USART_RX_INTERRUPT_BUFFERED ||
        (handle->mode & ADEV_USART_RX_MASK) ==
            ADEV_USART_RX_DMA_BUFFERED) {
        status = aOSWaitObjectCreate(&handle->rx_wait_object);
        if (status != A_STATUS_OK) {
            aOSWaitObjectDestroy(&handle->tx_wait_object);
            return status;
        }
    }

    return A_STATUS_OK;
}

static void wait_objects_destroy(aDevUsartHandle_t *handle)
{
    aOSWaitObjectDestroy(&handle->rx_wait_object);
    aOSWaitObjectDestroy(&handle->tx_wait_object);
}

static aStatus_t mutexes_create(aDevUsartHandle_t *handle)
{
    aStatus_t status = aOSMutexCreate(&handle->tx_mutex);

    if (status != A_STATUS_OK) {
        return status;
    }
    status = aOSMutexCreate(&handle->rx_mutex);
    if (status != A_STATUS_OK) {
        aOSMutexDestroy(&handle->tx_mutex);
        return status;
    }
    status = aOSMutexCreate(&handle->event_mutex);
    if (status != A_STATUS_OK) {
        aOSMutexDestroy(&handle->rx_mutex);
        aOSMutexDestroy(&handle->tx_mutex);
    }
    return status;
}

static void mutexes_destroy(aDevUsartHandle_t *handle)
{
    aOSMutexDestroy(&handle->rx_mutex);
    aOSMutexDestroy(&handle->tx_mutex);
    aOSMutexDestroy(&handle->event_mutex);
}

void aDevUsartConfigStructInit(aDevUsartConfig_t *config)
{
    if (config == NULL) {
        return;
    }

    aDrvUsartConfigStructInit(&config->drv_config);
    config->mode = ADEV_USART_TX_POLLING | ADEV_USART_RX_POLLING;
    config->interrupt_priority = 5U;
    config->rx_buffer = NULL;
    config->rx_buffer_size = 0U;
    config->tx_buffer = NULL;
    config->tx_buffer_size = 0U;
    config->rs485.enabled = A_FALSE;
    config->rs485.de_pin = ADRV_PIN_NONE;
    config->rs485.re_pin = ADRV_PIN_NONE;
    config->rs485.de_active_level = ADRV_GPIO_HIGH;
    config->rs485.re_active_level = ADRV_GPIO_LOW;
    config->rs485.receive_during_tx = A_FALSE;
}

static void handle_struct_init(aDevUsartHandle_t *handle, aBool_t dynamic)
{
    memset(handle, 0, sizeof(*handle));
    aDrvUsartHandleStructInit(&handle->drv_handle);
    aDrvGpioHandleStructInit(&handle->de_gpio);
    aDrvGpioHandleStructInit(&handle->re_gpio);
    handle->mode = ADEV_USART_TX_POLLING | ADEV_USART_RX_POLLING;
    aOSWorkItemInit(&handle->event_work);
    aOSWorkItemInit(&handle->tx_completion_work);
    aOSWorkItemInit(&handle->rx_completion_work);
    atomic_init(&handle->pending_events, 0U);
    atomic_init(&handle->tx_completion_claimed, A_TRUE);
    handle->dynamic_storage = dynamic;
}

static aStatus_t handle_init(const aDevUsartConfig_t *config,
                             aDevUsartHandle_t *handle)
{
    aStatus_t status;
    if ((config == NULL) || !mode_is_valid(config->mode)) {
        return A_STATUS_INVALID_PARAM;
    }
    /* Product capabilities are checked before allocating or touching hardware. */
    const aDevUsartMode_t tx = config->mode & ADEV_USART_TX_MASK;
    const aDevUsartMode_t rx = config->mode & ADEV_USART_RX_MASK;
    if (((tx == ADEV_USART_TX_INTERRUPT_BUFFERED) &&
         !ADEV_USART_HAS_INTERRUPT) ||
        ((tx == ADEV_USART_TX_DMA_BUFFERED) && !ADEV_USART_HAS_DMA) ||
        ((rx == ADEV_USART_RX_INTERRUPT_BUFFERED) &&
         !ADEV_USART_HAS_INTERRUPT) ||
        ((rx == ADEV_USART_RX_DMA_BUFFERED) &&
         !ADEV_USART_HAS_DMA) ||
        ((config->mode & ADEV_USART_OPTION_RX_IDLE) &&
         !ADEV_USART_HAS_INTERRUPT) ||
        (config->rs485.enabled && !ADEV_USART_HAS_RS485)) {
        return A_STATUS_UNSUPPORTED;
    }
    if (aOSValidateIsrPriority(config->interrupt_priority) != A_STATUS_OK) {
        return A_STATUS_INVALID_PARAM;
    }

    if (config->rs485.enabled &&
        ((config->rs485.de_pin == config->drv_config.tx_pin) ||
         (config->rs485.de_pin == config->drv_config.rx_pin) ||
         ((config->rs485.re_pin != ADRV_PIN_NONE) &&
          ((config->rs485.re_pin == config->drv_config.tx_pin) ||
           (config->rs485.re_pin == config->drv_config.rx_pin))))) {
        return A_STATUS_INVALID_PARAM;
    }

    status = aDrvUsartInitStatic(&config->drv_config, &handle->drv_handle);
    if (status != A_STATUS_OK) {
        return status;
    }
    handle->mode = config->mode;

#if ADEV_USART_HAS_RS485
    status = aDevUsartRS485Init(handle, &config->rs485);
#endif
    if (status == A_STATUS_OK) {
        status = mutexes_create(handle);
    }
    if (status == A_STATUS_OK) {
        status = wait_objects_create(handle);
    }
    if (status == A_STATUS_OK) {
        status = tx_mode_init(handle, config);
    }
    if (status == A_STATUS_OK) {
        status = aDevUsartRxModeInit(handle, config);
    }

    if (status != A_STATUS_OK) {
        (void)aDrvUsartDeInitStatic(&handle->drv_handle);
#if ADEV_USART_HAS_RS485
        (void)aDevUsartRS485DeInit(handle);
#endif
        wait_objects_destroy(handle);
        mutexes_destroy(handle);
        const aBool_t dynamic = handle->dynamic_storage;
        handle_struct_init(handle, dynamic);
    }
    return status;
}

aStatus_t aDevUsartInitStatic(const aDevUsartConfig_t *config,
                              aDevUsartStorage_t *storage,
                              aDevUsartHandle_t **handle_out)
{
    aDevUsartHandle_t *handle;
    aStatus_t status;

    if ((config == NULL) || (storage == NULL) || (handle_out == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    *handle_out = NULL;
    handle = (aDevUsartHandle_t *)(void *)storage->bytes;
    handle_struct_init(handle, A_FALSE);
    status = handle_init(config, handle);
    if (status == A_STATUS_OK) {
        *handle_out = handle;
    }
    return status;
}

aStatus_t aDevUsartCreate(const aDevUsartConfig_t *config,
                          aDevUsartHandle_t **handle_out)
{
    aDevUsartHandle_t *handle;
    aStatus_t status;

    if ((config == NULL) || (handle_out == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    *handle_out = NULL;
    handle = aOSAlloc(sizeof(*handle));
    if (handle == NULL) {
        return A_STATUS_NO_MEMORY;
    }
    handle_struct_init(handle, A_TRUE);
    status = handle_init(config, handle);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }

    if (!handle->drv_handle.initialized) {
        return A_STATUS_NOT_READY;
    }
    if ((handle->tx_state != ADEV_USART_TX_IDLE) ||
        (handle->rx_state != ADEV_USART_RX_IDLE) ||
        (handle->rx_request_head != NULL) ||
        (handle->rx_complete_head != NULL) ||
        handle->rs485_transmitting ||
        ((handle->tx_count != 0U) && (handle->tx_error == A_STATUS_OK)) ||
        (handle->tx_dma_active != 0U)) {
        return A_STATUS_BUSY;
    }

#if ADEV_USART_HAS_DMA
    if (handle->rx_dma_active) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_IDLE, A_FALSE);
        status = aDrvUsartAsyncRxAbort(&handle->drv_handle);
        if (status != A_STATUS_OK) return status;
        handle->rx_dma_active = A_FALSE;
    }

#endif

    aOSTimerStop(handle->tx_deadline_timer);
    aOSTimerStop(handle->rx_deadline_timer);
    status = aOSWorkWaitIdle(&handle->tx_completion_work,
                             A_TIMEOUT_FOREVER);
    if (status == A_STATUS_OK) {
        status = aOSWorkWaitIdle(&handle->rx_completion_work,
                                 A_TIMEOUT_FOREVER);
    }
    if (status != A_STATUS_OK) return status;
    aOSTimerDestroy(&handle->tx_deadline_timer);
    aOSTimerDestroy(&handle->rx_deadline_timer);

    status = aDevUsartUnregisterEventCallback(handle);
    if (status == A_STATUS_OK) {
        status = aDrvUsartDeInitStatic(&handle->drv_handle);
    }
    if (status == A_STATUS_OK) {
        status = aOSWorkWaitIdle(&handle->event_work, A_TIMEOUT_FOREVER);
    }
    if (status == A_STATUS_OK) {
#if ADEV_USART_HAS_RS485
        status = aDevUsartRS485DeInit(handle);
#endif
        wait_objects_destroy(handle);
        mutexes_destroy(handle);
        const aBool_t dynamic = handle->dynamic_storage;
        handle_struct_init(handle, dynamic);
    }
    return status;
}

aStatus_t aDevUsartDestroy(aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if ((handle == NULL) || !handle->dynamic_storage) {
        return A_STATUS_INVALID_PARAM;
    }
    status = handle->drv_handle.initialized
                 ? aDevUsartDeInit(handle)
                 : A_STATUS_OK;
    if (status == A_STATUS_OK) {
        aOSFree(handle);
    }
    return status;
}

aStatus_t aDevUsartRegisterEventCallback(
    aDevUsartHandle_t *handle,
    aDevUsartEventCallback_t callback,
    void *argument)
{
    if ((handle == NULL) || (callback == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->drv_handle.initialized) {
        return A_STATUS_NOT_READY;
    }

    const aStatus_t status = aOSMutexLock(
        handle->event_mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    atomic_store_explicit(&handle->pending_events, 0U, memory_order_release);
    handle->event_argument = argument;
    handle->event_callback = callback;
    (void)aOSMutexUnlock(handle->event_mutex);
    return status;
}

aStatus_t aDevUsartUnregisterEventCallback(
    aDevUsartHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->drv_handle.initialized) {
        return A_STATUS_NOT_READY;
    }

    const aStatus_t status = aOSMutexLock(
        handle->event_mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    atomic_store_explicit(&handle->pending_events, 0U, memory_order_release);
    handle->event_callback = NULL;
    handle->event_argument = NULL;
    (void)aOSMutexUnlock(handle->event_mutex);
    return status;
}

#if ADEV_USART_HAS_DMA || ADEV_USART_HAS_INTERRUPT
static aStatus_t wait_for_event(void *wait_object,
                                const aTimepoint_t *end)
{
    return aOSWaitObjectWait(
        wait_object, aTimepointRemaining(end, aOSGetUptimeMs()));
}

#endif

static aSSize_t fail_with_wait_status(aStatus_t status,
                                      aTimeout_t timeout)
{
    return ((status == A_STATUS_BUSY) ||
            (status == A_STATUS_TIMEOUT))
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
}

static aSSize_t polling_read(aDevUsartHandle_t *handle, void *buffer,
                             size_t buffer_size, const aTimepoint_t *end,
                             aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < buffer_size) {
        const aStatus_t status = aDrvUsartTryReadByte(
            &handle->drv_handle, (uint8_t *)buffer + count);

        if (status == A_STATUS_OK) {
            ++count;
        } else if (count != 0U) {
            return (aSSize_t)count;
        } else if (status != A_STATUS_BUSY) {
            return aOSFailWithStatus(status);
        } else if (aOSPollWaitExpired(end)) {
            return aOSFailWithTimeout(original_timeout);
        }
    }
    return (aSSize_t)count;
}

#if ADEV_USART_HAS_INTERRUPT
static aSSize_t buffered_read(aDevUsartHandle_t *handle, void *buffer,
                              size_t buffer_size,
                              const aTimepoint_t *end,
                              aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < buffer_size) {
        aBool_t available;
        aStatus_t status = A_STATUS_OK;

        aOSCriticalEnter();
        available = handle->rx_count != 0U;
        if (available) {
            ((uint8_t *)buffer)[count] = handle->rx_buffer[handle->rx_tail];
            handle->rx_tail = (handle->rx_tail + 1U) % handle->rx_buffer_size;
            --handle->rx_count;
        }
        aOSCriticalExit();

        if (available) {
            ++count;
        } else if (count != 0U) {
            return (aSSize_t)count;
        } else if (handle->rx_wait_object != NULL) {
            status = wait_for_event(handle->rx_wait_object, end);
            if (status != A_STATUS_OK) {
                return fail_with_wait_status(status, original_timeout);
            }
        } else if (aOSPollWaitExpired(end)) {
            return aOSFailWithTimeout(original_timeout);
        }
    }
    return (aSSize_t)count;
}

#endif

#if ADEV_USART_HAS_DMA
static aSSize_t dma_buffered_read(aDevUsartHandle_t *handle, void *buffer,
                                  size_t buffer_size,
                                  const aTimepoint_t *end,
                                  aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < buffer_size) {
        size_t copied = 0U;
        aStatus_t status = aDevUsartDmaRxCopy(
            handle, (uint8_t *)buffer + count, buffer_size - count, &copied);
        count += copied;

        if (count == buffer_size) break;
        if (count != 0U) return (aSSize_t)count;
        if (status != A_STATUS_OK && status != A_STATUS_BUSY) {
            return aOSFailWithStatus(status);
        }
        if (handle->rx_wait_object != NULL) {
            (void)aOSMutexUnlock(handle->rx_mutex);
            status = wait_for_event(handle->rx_wait_object, end);
            const aStatus_t lock_status = aOSMutexLock(
                handle->rx_mutex, A_TIMEOUT_FOREVER);
            if (lock_status != A_STATUS_OK) {
                return aOSFailWithStatus(lock_status);
            }
            if (status != A_STATUS_OK) {
                return fail_with_wait_status(status, original_timeout);
            }
        } else if (aOSPollWaitExpired(end)) {
            return aOSFailWithTimeout(original_timeout);
        }
    }
    return (aSSize_t)count;
}

#endif

aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *buffer,
                       size_t buffer_size, aTimeout_t timeout)
{
    aTimepoint_t end;
    aStatus_t status;
    aSSize_t result;

    if ((handle == NULL) || !aTimeoutIsValid(timeout) ||
        (buffer_size > (size_t)PTRDIFF_MAX) ||
        ((buffer == NULL) && (buffer_size != 0U))) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (!handle->drv_handle.initialized) {
        return aOSFailWithStatus(A_STATUS_NOT_READY);
    }
    if (buffer_size == 0U) {
        return 0;
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(
        handle->rx_mutex,
        aTimepointRemaining(&end, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) {
        return fail_with_wait_status(status, timeout);
    }
    const aDevUsartMode_t rx_mode = handle->mode & ADEV_USART_RX_MASK;
    if ((handle->rx_state != ADEV_USART_RX_IDLE) ||
        ((rx_mode != ADEV_USART_RX_DMA_BUFFERED) &&
         (handle->rx_request_head != NULL))) {
        (void)aOSMutexUnlock(handle->rx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }

    handle->rx_state = ADEV_USART_RX_STREAM;
#if ADEV_USART_HAS_DMA
    if (rx_mode == ADEV_USART_RX_DMA_BUFFERED) {
        result = dma_buffered_read(handle, buffer, buffer_size, &end, timeout);
    } else
#endif
#if ADEV_USART_HAS_INTERRUPT
    if (rx_mode == ADEV_USART_RX_INTERRUPT_BUFFERED) {
        result = buffered_read(handle, buffer, buffer_size, &end, timeout);
    } else
#endif
    {
        result = polling_read(handle, buffer, buffer_size, &end, timeout);
    }
    handle->rx_state = ADEV_USART_RX_IDLE;
    (void)aOSMutexUnlock(handle->rx_mutex);
    return result;
}

static aSSize_t polling_write(aDevUsartHandle_t *handle, const void *data,
                              size_t data_size,
                              const aTimepoint_t *end,
                              aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < data_size) {
        const aStatus_t status = aDrvUsartTryWriteByte(
            &handle->drv_handle, ((const uint8_t *)data)[count]);

        if (status == A_STATUS_OK) {
            ++count;
        } else if (status != A_STATUS_BUSY) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithStatus(status);
        } else if (aOSPollWaitExpired(end)) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithTimeout(original_timeout);
        }
    }
    return (aSSize_t)count;
}

#if ADEV_USART_HAS_DMA
static aStatus_t dma_tx_start_locked(aDevUsartHandle_t *handle)
{
    size_t contiguous;
    size_t started = 0U;
    aStatus_t status;

    if ((handle->tx_dma_active != 0U) || (handle->tx_count == 0U)) {
        return A_STATUS_OK;
    }

    contiguous = handle->tx_buffer_size - handle->tx_tail;
    if (contiguous > handle->tx_count) {
        contiguous = handle->tx_count;
    }
    status = A_STATUS_OK;
#if ADEV_USART_HAS_RS485
    status = aDevUsartRS485Begin(handle);
#endif
    if (status != A_STATUS_OK) {
        return status;
    }
    status = aDrvUsartAsyncTxStart(
        &handle->drv_handle, &handle->tx_buffer[handle->tx_tail],
        contiguous, &started);
    if (status != A_STATUS_OK) {
#if ADEV_USART_HAS_RS485
        aDevUsartRs485ArmComplete(handle);
#endif
        return status;
    }

    handle->tx_dma_active = started;
    status = aDrvUsartSetInterruptEnabled(
        &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
    if (status != A_STATUS_OK) {
        (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
        handle->tx_dma_active = 0U;
#if ADEV_USART_HAS_RS485
        aDevUsartRs485ArmComplete(handle);
#endif
    }
    return status;
}

#endif

#if ADEV_USART_HAS_DMA
static size_t ring_write(aDevUsartHandle_t *handle, const uint8_t *data,
                         size_t size)
{
    size_t writable = handle->tx_buffer_size - handle->tx_count;
    size_t first;

    if (writable > size) {
        writable = size;
    }
    first = handle->tx_buffer_size - handle->tx_head;
    if (first > writable) {
        first = writable;
    }

    memcpy(&handle->tx_buffer[handle->tx_head], data, first);
    memcpy(handle->tx_buffer, data + first, writable - first);
    handle->tx_head = (handle->tx_head + writable) %
                      handle->tx_buffer_size;
    handle->tx_count += writable;
    return writable;
}

#endif

#if ADEV_USART_HAS_DMA
static aSSize_t dma_buffered_write(aDevUsartHandle_t *handle,
                                   const void *data, size_t data_size,
                                   const aTimepoint_t *end,
                                   aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < data_size) {
        size_t accepted = 0U;
        aStatus_t status;

        aOSCriticalEnter();
        status = handle->tx_error;
        if (status == A_STATUS_OK) {
            accepted = ring_write(
                handle, (const uint8_t *)data + count,
                data_size - count);
            status = dma_tx_start_locked(handle);
            if (status != A_STATUS_OK) {
                handle->tx_error = status;
            }
        }
        aOSCriticalExit();

        count += accepted;
        if (status != A_STATUS_OK) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithStatus(status);
        }
        if (accepted == 0U) {
            status = wait_for_event(handle->tx_wait_object, end);
            if (status != A_STATUS_OK) {
                return count != 0U ? (aSSize_t)count
                                   : fail_with_wait_status(
                                         status, original_timeout);
            }
        }
    }
    return (aSSize_t)count;
}

#endif

#if ADEV_USART_HAS_INTERRUPT
static aSSize_t interrupt_write(aDevUsartHandle_t *handle, const void *data,
                                size_t data_size,
                                const aTimepoint_t *end,
                                aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < data_size) {
        aBool_t space_available;
        aStatus_t status = A_STATUS_OK;

        aOSCriticalEnter();
        space_available = handle->tx_count < handle->tx_buffer_size;
        status = handle->tx_error;
        if (space_available && (status == A_STATUS_OK)) {
#if ADEV_USART_HAS_RS485
            status = aDevUsartRS485Begin(handle);
#endif
        }
        if (status != A_STATUS_OK) {
            aOSCriticalExit();
            return count != 0U ? (aSSize_t)count : aOSFailWithStatus(status);
        }
        if (space_available) {
            handle->tx_buffer[handle->tx_head] = ((const uint8_t *)data)[count];
            handle->tx_head = (handle->tx_head + 1U) % handle->tx_buffer_size;
            ++handle->tx_count;
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TXE, A_TRUE);
        }
        aOSCriticalExit();

        if (space_available) {
            ++count;
        } else {
            status = wait_for_event(handle->tx_wait_object, end);
            if (status != A_STATUS_OK) {
                return count != 0U ? (aSSize_t)count
                                   : fail_with_wait_status(
                                         status, original_timeout);
            }
        }
    }
    return (aSSize_t)count;
}

#endif

aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *data,
                        size_t data_size, aTimeout_t timeout)
{
    aTimepoint_t end;
    aStatus_t status;
    aSSize_t result;

    if ((handle == NULL) || !aTimeoutIsValid(timeout) ||
        (data_size > (size_t)PTRDIFF_MAX) ||
        ((data == NULL) && (data_size != 0U))) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (!handle->drv_handle.initialized) {
        return aOSFailWithStatus(A_STATUS_NOT_READY);
    }
    if (data_size == 0U) {
        return 0;
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(
        handle->tx_mutex,
        aTimepointRemaining(&end, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) {
        return fail_with_wait_status(status, timeout);
    }
    if ((handle->tx_state != ADEV_USART_TX_IDLE) ||
        (handle->tx_callback != NULL) || (handle->tx_queue_owner != NULL)) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }

    handle->tx_state = ADEV_USART_TX_STREAM;
    switch (handle->mode & ADEV_USART_TX_MASK) {
#if ADEV_USART_HAS_DMA
    case ADEV_USART_TX_DMA_BUFFERED:
        result = dma_buffered_write(handle, data, data_size, &end,
                                    timeout);
        break;
#endif

#if ADEV_USART_HAS_INTERRUPT
    case ADEV_USART_TX_INTERRUPT_BUFFERED:
        result = interrupt_write(handle, data, data_size, &end,
                                 timeout);
        break;
#endif

    case ADEV_USART_TX_POLLING:
    default:
        aOSCriticalEnter();
        status = handle->tx_error;
        if (status == A_STATUS_OK) {
#if ADEV_USART_HAS_RS485
            status = aDevUsartRS485Begin(handle);
#endif
        }
#if ADEV_USART_HAS_RS485
        if (handle->rs485.enabled) {
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
        }
#endif

        aOSCriticalExit();
        result = status == A_STATUS_OK
                     ? polling_write(handle, data, data_size, &end, timeout)
                     : aOSFailWithStatus(status);
        aOSCriticalEnter();
#if ADEV_USART_HAS_RS485
        aDevUsartRs485ArmComplete(handle);
#endif
        aOSCriticalExit();
        break;
    }
    handle->tx_state = ADEV_USART_TX_IDLE;
    (void)aOSMutexUnlock(handle->tx_mutex);
    return result;
}

static aStatus_t wait_transmit_complete_locked(
    aDevUsartHandle_t *handle, const aTimepoint_t *end,
    aTimeout_t original_timeout)
{
    for (;;) {
        aBool_t complete;
        aStatus_t status;

        if (handle->tx_error != A_STATUS_OK) {
            return handle->tx_error;
        }
        status = aDrvUsartIsTransmitComplete(
            &handle->drv_handle, &complete);

        if (status != A_STATUS_OK) {
            return status;
        }
        if (complete &&
            (((handle->mode & ADEV_USART_TX_MASK) ==
              ADEV_USART_TX_POLLING) ||
             ((handle->tx_count == 0U) &&
              (handle->tx_dma_active == 0U)))) {
            aOSCriticalEnter();
            rs485_complete(handle);
            aOSCriticalExit();
            return handle->tx_error;
        }
#if ADEV_USART_HAS_DMA || ADEV_USART_HAS_INTERRUPT
        if (((handle->mode & ADEV_USART_TX_MASK) ==
             ADEV_USART_TX_INTERRUPT_BUFFERED) ||
            ((handle->mode & ADEV_USART_TX_MASK) ==
             ADEV_USART_TX_DMA_BUFFERED)) {
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
            const aStatus_t wait_status = wait_for_event(
                handle->tx_wait_object, end);

            if (wait_status != A_STATUS_OK) {
                if ((wait_status == A_STATUS_BUSY) ||
                    (wait_status == A_STATUS_TIMEOUT)) {
                    return ((original_timeout.type ==
                             A_TIMEOUT_TYPE_RELATIVE) &&
                            (original_timeout.milliseconds == 0U))
                               ? A_STATUS_BUSY
                               : A_STATUS_TIMEOUT;
                }
                return wait_status;
            }
            continue;
        }
#endif

        if (aOSPollWaitExpired(end)) {
            return original_timeout.milliseconds == 0U
                       ? A_STATUS_BUSY
                       : A_STATUS_TIMEOUT;
        }
    }
}

aStatus_t aDevUsartWaitTransmitComplete(aDevUsartHandle_t *handle,
                                        aTimeout_t timeout)
{
    aTimepoint_t end;
    aStatus_t status;

    if ((handle == NULL) || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->drv_handle.initialized) {
        return A_STATUS_NOT_READY;
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(
        handle->tx_mutex,
        aTimepointRemaining(&end, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) {
        return status == A_STATUS_BUSY && timeout.milliseconds != 0U
                   ? A_STATUS_TIMEOUT
                   : status;
    }
    if ((handle->tx_state != ADEV_USART_TX_IDLE) &&
        (handle->tx_state != ADEV_USART_TX_QUEUE)) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return A_STATUS_BUSY;
    }

    const aBool_t queue_claimed =
        handle->tx_state == ADEV_USART_TX_QUEUE ? A_TRUE : A_FALSE;
    if (!queue_claimed) handle->tx_state = ADEV_USART_TX_STREAM;
    status = wait_transmit_complete_locked(handle, &end, timeout);
    if (!queue_claimed) handle->tx_state = ADEV_USART_TX_IDLE;
    (void)aOSMutexUnlock(handle->tx_mutex);
    return status;
}

#if ADEV_USART_HAS_ASYNC
static aStatus_t write_async_submit(aDevUsartHandle_t *handle,
                                    const void *owner,
                                    const aDevUsartWriteRequest_t *request)
{
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    const void *buffer = request->buffer;
    const size_t size = request->size;
    const aTimeout_t timeout = request->timeout;
    const aDevUsartTxCallback_t callback = request->callback;
    void *argument = request->argument;
    aStatus_t status;
    size_t started = 0U;

    if ((handle == NULL) || (buffer == NULL) || (size == 0U) ||
        (size > 65535U) || !aTimeoutIsValid(timeout) ||
        (timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
         timeout.milliseconds == 0U) || (callback == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->drv_handle.initialized) return A_STATUS_NOT_READY;
    if (!ADEV_USART_HAS_ASYNC ||
        !aDrvUsartAsyncTxIsSupported(&handle->drv_handle)) {
        return A_STATUS_UNSUPPORTED;
    }
    status = aOSMutexLock(handle->tx_mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    if (((owner == NULL) &&
         ((handle->tx_state != ADEV_USART_TX_IDLE) ||
          (handle->tx_queue_owner != NULL))) ||
        ((owner != NULL) &&
         ((owner != handle->tx_queue_owner) ||
          (handle->tx_state != ADEV_USART_TX_QUEUE))) ||
        (handle->tx_callback != NULL) ||
        (handle->tx_count != 0U) || (handle->tx_dma_active != 0U)) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return A_STATUS_BUSY;
    }

    if (timeout.type != A_TIMEOUT_TYPE_FOREVER) {
        if (handle->tx_deadline_timer == NULL) {
            status = aOSTimerCreate(&handle->tx_deadline_timer,
                                    aDevUsartAsyncTxTimeout, handle);
            if (status != A_STATUS_OK) {
                (void)aOSMutexUnlock(handle->tx_mutex);
                return status;
            }
        }
    }

    status = A_STATUS_OK;
#if ADEV_USART_HAS_RS485
    status = aDevUsartRS485Begin(handle);
#endif
    if (status != A_STATUS_OK) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return status;
    }
    handle->tx_async_buffer = buffer;
    handle->tx_async_size = size;
    handle->tx_callback = callback;
    handle->tx_callback_argument = argument;
    handle->tx_state = ADEV_USART_TX_ASYNC;
    atomic_store_explicit(&handle->tx_completion_claimed, A_FALSE,
                          memory_order_release);
    status = aDrvUsartAsyncTxStart(&handle->drv_handle, buffer, size,
                                   &started);
    if ((status == A_STATUS_OK) && (started != size)) {
        status = A_STATUS_ERROR;
        (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
    }
    if (status == A_STATUS_OK) {
        handle->tx_dma_active = size;
        status = aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
    }
    if ((status == A_STATUS_OK) &&
        (timeout.type != A_TIMEOUT_TYPE_FOREVER)) {
        status = aOSTimerStart(handle->tx_deadline_timer,
                               timeout.milliseconds);
    }
    if (status != A_STATUS_OK) {
        (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
        rs485_complete(handle);
        handle->tx_state = ADEV_USART_TX_IDLE;
        handle->tx_dma_active = 0U;
        handle->tx_callback = NULL;
        handle->tx_callback_argument = NULL;
        handle->tx_async_buffer = NULL;
        handle->tx_async_size = 0U;
    }
    (void)aOSMutexUnlock(handle->tx_mutex);
    return status;
}

aStatus_t aDevUsartWriteAsync(aDevUsartHandle_t *handle,
                              const aDevUsartWriteRequest_t *request)
{
    return write_async_submit(handle, NULL, request);
}

aStatus_t aDevUsartWriteAsyncQueued(aDevUsartHandle_t *handle,
                                    const void *owner,
                                    const aDevUsartWriteRequest_t *request)
{
    if (owner == NULL) return A_STATUS_INVALID_PARAM;
    return write_async_submit(handle, owner, request);
}

aStatus_t aDevUsartTxQueueClaim(aDevUsartHandle_t *handle,
                                const void *owner)
{
    aStatus_t status;

    if ((handle == NULL) || (owner == NULL)) return A_STATUS_INVALID_PARAM;
    if (!handle->drv_handle.initialized) return A_STATUS_NOT_READY;
    status = aOSMutexLock(handle->tx_mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    if ((handle->tx_state != ADEV_USART_TX_IDLE) ||
        (handle->tx_callback != NULL) || (handle->tx_count != 0U) ||
        (handle->tx_dma_active != 0U)) {
        status = A_STATUS_BUSY;
    } else {
        handle->tx_queue_owner = owner;
        handle->tx_state = ADEV_USART_TX_QUEUE;
    }
    (void)aOSMutexUnlock(handle->tx_mutex);
    return status;
}

aStatus_t aDevUsartTxQueueRelease(aDevUsartHandle_t *handle,
                                  const void *owner)
{
    aStatus_t status;

    if ((handle == NULL) || (owner == NULL)) return A_STATUS_INVALID_PARAM;
    status = aOSMutexLock(handle->tx_mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    if (handle->tx_queue_owner != owner) {
        status = A_STATUS_INVALID_PARAM;
    } else if ((handle->tx_state != ADEV_USART_TX_QUEUE) ||
               (handle->tx_callback != NULL) || (handle->tx_dma_active != 0U)) {
        status = A_STATUS_BUSY;
    } else {
        handle->tx_queue_owner = NULL;
        handle->tx_state = ADEV_USART_TX_IDLE;
        status = A_STATUS_OK;
    }
    (void)aOSMutexUnlock(handle->tx_mutex);
    return status;
}

static aStatus_t write_async_cancel(aDevUsartHandle_t *handle,
                                    const void *owner)
{
    aStatus_t status;
    size_t remaining = 0U;
    size_t transferred = 0U;

    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->drv_handle.initialized) return A_STATUS_NOT_READY;
    status = aOSMutexLock(handle->tx_mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    if (((owner == NULL) && (handle->tx_queue_owner != NULL)) ||
        ((owner != NULL) && (owner != handle->tx_queue_owner))) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return A_STATUS_BUSY;
    }
    if (handle->tx_state != ADEV_USART_TX_ASYNC) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return A_STATUS_NOT_READY;
    }
    aOSTimerStop(handle->tx_deadline_timer);
    if (aDrvUsartAsyncTxGetRemaining(&handle->drv_handle, &remaining) ==
        A_STATUS_OK) {
        transferred = handle->tx_async_size - remaining;
    }
    (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
    if (!handle->rs485.enabled) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
    }
    async_tx_complete(handle, A_STATUS_CANCELLED, transferred, A_FALSE);
    (void)aOSMutexUnlock(handle->tx_mutex);
    return A_STATUS_OK;
}

aStatus_t aDevUsartWriteAsyncCancel(aDevUsartHandle_t *handle)
{
    return write_async_cancel(handle, NULL);
}

aStatus_t aDevUsartWriteAsyncCancelQueued(aDevUsartHandle_t *handle,
                                          const void *owner)
{
    if (owner == NULL) return A_STATUS_INVALID_PARAM;
    return write_async_cancel(handle, owner);
}

#endif

uint32_t aDevUsartGetIdleEventCount(const aDevUsartHandle_t *handle)
{
    return handle == NULL ? 0U : handle->idle_event_count;
}

aBool_t aDevUsartHasRxOverflowed(const aDevUsartHandle_t *handle)
{
    return (handle != NULL) && handle->rx_overflow;
}

void aDevUsartClearRxOverflow(aDevUsartHandle_t *handle)
{
    if (handle != NULL) {
        handle->rx_overflow = A_FALSE;
    }
}

aStatus_t aDevUsartGetRxError(const aDevUsartHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->drv_handle.initialized) {
        return A_STATUS_NOT_READY;
    }
    return handle->rx_error;
}

aBool_t aDevUsartIsSupported(const aDevUsartHandle_t *handle,
                             aDevUsartCapability_t capability)
{
    if ((handle == NULL) || !handle->drv_handle.initialized) {
        return A_FALSE;
    }

#if ADEV_USART_HAS_DMA
    switch (capability) {
    case ADEV_USART_CAP_TX_DIRECT:
        return ADEV_USART_HAS_DMA &&
               aDrvUsartAsyncTxIsSupported(&handle->drv_handle);
    case ADEV_USART_CAP_RX_DIRECT:
        return ADEV_USART_HAS_DMA &&
               aDrvUsartAsyncRxIsSupported(&handle->drv_handle);
    default:
        return A_FALSE;
    }
#else
    (void)capability;
    return A_FALSE;
#endif
}
