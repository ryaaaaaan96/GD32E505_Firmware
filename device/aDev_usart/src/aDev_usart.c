#include "aDev_usart_internal.h"

#include <limits.h>
#include <string.h>

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

#if ADEV_USART_NEEDS_IRQ
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

static aStatus_t wait_objects_create(aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if (ADEV_USART_HAS_DMA || ((handle->mode & ADEV_USART_TX_MASK) ==
         ADEV_USART_TX_INTERRUPT_BUFFERED) ||
        ((handle->mode & ADEV_USART_TX_MASK) ==
         ADEV_USART_TX_DMA_BUFFERED)) {
        status = aOSWaitObjectCreate(&handle->tx_wait_object);
        if (status != A_STATUS_OK) {
            return status;
        }
    }

    if (ADEV_USART_HAS_DMA || (handle->mode & ADEV_USART_RX_MASK) ==
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
    handle->interrupt_priority = config->interrupt_priority;

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
        status = aDevUsartTxModeInit(handle, config);
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
    /* A worker must never wait on another item in its own serialized queue. */
    if (aOSIsWorkContext()) return A_STATUS_BUSY;

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
