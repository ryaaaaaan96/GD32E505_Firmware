#include "aDev_usart.h"

#include "aOS.h"

#include <limits.h>
#include <string.h>

static aStatus_t dma_tx_start_locked(aDevUsartHandle_t *handle);
#include <string.h>

static aStatus_t dma_tx_start_locked(aDevUsartHandle_t *handle);

static void notify_event(aDevUsartHandle_t *handle,
                         aDevUsartEvent_t event)
{
    if (handle->event_callback != NULL) {
        handle->event_callback(event, handle->event_argument);
    }
}

static void irq_receive(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    uint8_t data;

    if (aDrvUsartTryReadByte(&handle->drv_handle, &data) != A_STATUS_OK) {
        return;
    }

    if (handle->rx_count >= handle->rx_buffer_size) {
        handle->rx_overflow = A_TRUE;
        return;
    }

    handle->rx_buffer[handle->rx_head] = data;
    handle->rx_head = (handle->rx_head + 1U) % handle->rx_buffer_size;
    ++handle->rx_count;
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    notify_event(handle, ADEV_USART_EVENT_RX_READY);
}

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
        notify_event(handle, ADEV_USART_EVENT_TX_SPACE);
        if (handle->tx_count == 0U) {
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TXE, A_FALSE);
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
        }
    }
}

static void irq_transmit_complete(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    if ((handle->mode & ADEV_USART_TX_MASK) ==
        ADEV_USART_TX_DMA_BUFFERED) {
        size_t remaining = 0U;
        aStatus_t status;

        if (handle->tx_dma_active == 0U) {
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
            aOSWaitObjectNotifyFromISR(handle->tx_wait_object);
            notify_event(handle, ADEV_USART_EVENT_TX_COMPLETE);
            return;
        }

        status = aDrvUsartAsyncTxGetRemaining(
            &handle->drv_handle, &remaining);
        if ((status == A_STATUS_OK) && (remaining != 0U)) {
            return;
        }
        if (status != A_STATUS_OK) {
            handle->tx_error = status;
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
        notify_event(handle, ADEV_USART_EVENT_TX_SPACE);
        if ((handle->tx_count == 0U) ||
            (handle->tx_error != A_STATUS_OK)) {
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
            notify_event(handle, ADEV_USART_EVENT_TX_COMPLETE);
        }
        return;
    }

    (void)aDrvUsartSetInterruptEnabled(
        &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
    aOSWaitObjectNotifyFromISR(handle->tx_wait_object);
    notify_event(handle, ADEV_USART_EVENT_TX_COMPLETE);
}

static void irq_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->idle_event_count;
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    notify_event(handle, ADEV_USART_EVENT_RX_IDLE);
}

static aStatus_t dma_rx_commit(aDevUsartHandle_t *handle)
{
    size_t received = handle->rx_dma_observed;
    size_t added;
    size_t free_space;
    aStatus_t status;

    status = aDrvUsartAsyncRxGetReceivedCount(&handle->drv_handle,
                                              &received);
    added = received - handle->rx_dma_observed;
    handle->rx_dma_observed = received;

    if (added == 0U) {
        if (status != A_STATUS_OK) {
            handle->rx_overflow = A_TRUE;
        }
        return status;
    }

    free_space = handle->rx_buffer_size - handle->rx_count;
    if (added >= handle->rx_buffer_size) {
        handle->rx_tail = received % handle->rx_buffer_size;
        handle->rx_count = handle->rx_buffer_size;
        handle->rx_overflow = A_TRUE;
    } else if (added > free_space) {
        const size_t discarded = added - free_space;

        handle->rx_tail =
            (handle->rx_tail + discarded) % handle->rx_buffer_size;
        handle->rx_count = handle->rx_buffer_size;
        handle->rx_overflow = A_TRUE;
    } else {
        handle->rx_count += added;
    }
    handle->rx_head = received % handle->rx_buffer_size;

    if (status != A_STATUS_OK) {
        handle->rx_overflow = A_TRUE;
    }
    return status;
}

static void dma_rx_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    (void)dma_rx_commit(handle);
    ++handle->idle_event_count;
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    notify_event(handle, ADEV_USART_EVENT_RX_READY);
    notify_event(handle, ADEV_USART_EVENT_RX_IDLE);
}

static aStatus_t register_irq_callback(aDevUsartHandle_t *handle,
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
    case ADEV_USART_TX_DMA_BUFFERED:
        break;
    default:
        return A_FALSE;
    }

    switch (rx_mode) {
    case ADEV_USART_RX_POLLING:
    case ADEV_USART_RX_INTERRUPT_BUFFERED:
    case ADEV_USART_RX_DMA_CIRCULAR:
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
        return A_STATUS_OK;
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
        status = register_irq_callback(
            handle, ADRV_USART_EXTI_TXE, irq_transmit,
            config->interrupt_priority, A_FALSE);
        if (status != A_STATUS_OK) {
            return status;
        }
        return register_irq_callback(
            handle, ADRV_USART_EXTI_TC, irq_transmit_complete,
            config->interrupt_priority, A_FALSE);
    }
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
        status = register_irq_callback(
            handle, ADRV_USART_EXTI_TC, irq_transmit_complete,
            config->interrupt_priority, A_FALSE);
        return status;
    }
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
        status = register_irq_callback(
            handle, ADRV_USART_EXTI_TC, irq_transmit_complete,
            config->interrupt_priority, A_FALSE);
        return status;
    }
    default:
        return A_STATUS_INVALID_PARAM;
    }
}

static aStatus_t rx_idle_detection_enable(
    aDevUsartHandle_t *handle, const aDevUsartConfig_t *config)
{
    aDrvInterruptCallback_t callback = irq_idle;

    if (!aDrvUsartInterruptIsSupported()) {
        return A_STATUS_UNSUPPORTED;
    }
    if ((config->mode & ADEV_USART_RX_MASK) ==
        ADEV_USART_RX_DMA_CIRCULAR) {
        callback = dma_rx_idle;
    }
    return register_irq_callback(
        handle, ADRV_USART_EXTI_IDLE, callback,
        config->interrupt_priority, A_TRUE);
}

static aStatus_t rx_mode_init(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config)
{
    aStatus_t status;

    switch (config->mode & ADEV_USART_RX_MASK) {
    case ADEV_USART_RX_POLLING:
        status = A_STATUS_OK;
        break;
    case ADEV_USART_RX_INTERRUPT_BUFFERED:
        if (!aDrvUsartInterruptIsSupported()) {
            return A_STATUS_UNSUPPORTED;
        }
        if ((config->rx_buffer == NULL) ||
            (config->rx_buffer_size < 2U)) {
            return A_STATUS_INVALID_PARAM;
        }
        handle->rx_buffer = config->rx_buffer;
        handle->rx_buffer_size = config->rx_buffer_size;
        status = register_irq_callback(
            handle, ADRV_USART_EXTI_RXNE, irq_receive,
            config->interrupt_priority, A_TRUE);
        break;
    case ADEV_USART_RX_DMA_CIRCULAR:
        if (!aDrvUsartAsyncRxIsSupported(&handle->drv_handle)) {
            return A_STATUS_UNSUPPORTED;
        }
        if ((config->rx_buffer == NULL) ||
            (config->rx_buffer_size < 2U) ||
            (config->rx_buffer_size > 65535U)) {
            return A_STATUS_INVALID_PARAM;
        }
        handle->rx_buffer = config->rx_buffer;
        handle->rx_buffer_size = config->rx_buffer_size;
        status = aDrvUsartAsyncRxCircularStart(
            &handle->drv_handle, handle->rx_buffer,
            handle->rx_buffer_size, config->interrupt_priority);
        break;
    default:
        return A_STATUS_INVALID_PARAM;
    }

    if ((status == A_STATUS_OK) &&
        ((config->mode & ADEV_USART_OPTION_RX_IDLE) != 0U)) {
        status = rx_idle_detection_enable(handle, config);
    }
    return status;
}

static aStatus_t wait_objects_create(aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if (((handle->mode & ADEV_USART_TX_MASK) ==
         ADEV_USART_TX_INTERRUPT_BUFFERED) ||
        ((handle->mode & ADEV_USART_TX_MASK) ==
         ADEV_USART_TX_DMA_BUFFERED)) {
    if (((handle->mode & ADEV_USART_TX_MASK) ==
         ADEV_USART_TX_INTERRUPT_BUFFERED) ||
        ((handle->mode & ADEV_USART_TX_MASK) ==
         ADEV_USART_TX_DMA_BUFFERED)) {
        status = aOSWaitObjectCreate(&handle->tx_wait_object);
        if (status != A_STATUS_OK) {
            return status;
        }
    }

    if (((handle->mode & ADEV_USART_RX_MASK) ==
         ADEV_USART_RX_INTERRUPT_BUFFERED) ||
        (((handle->mode & ADEV_USART_RX_MASK) ==
          ADEV_USART_RX_DMA_CIRCULAR) &&
         ((handle->mode & ADEV_USART_OPTION_RX_IDLE) != 0U))) {
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
    }
    return status;
}

static void mutexes_destroy(aDevUsartHandle_t *handle)
{
    aOSMutexDestroy(&handle->rx_mutex);
    aOSMutexDestroy(&handle->tx_mutex);
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
    }
    return status;
}

static void mutexes_destroy(aDevUsartHandle_t *handle)
{
    aOSMutexDestroy(&handle->rx_mutex);
    aOSMutexDestroy(&handle->tx_mutex);
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
}

void aDevUsartHandleStructInit(aDevUsartHandle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    aDrvUsartHandleStructInit(&handle->drv_handle);
    handle->mode = ADEV_USART_TX_POLLING | ADEV_USART_RX_POLLING;
    handle->rx_buffer = NULL;
    handle->rx_buffer_size = 0U;
    handle->rx_dma_observed = 0U;
    handle->rx_head = 0U;
    handle->rx_tail = 0U;
    handle->rx_count = 0U;
    handle->tx_buffer = NULL;
    handle->tx_buffer_size = 0U;
    handle->tx_head = 0U;
    handle->tx_tail = 0U;
    handle->tx_count = 0U;
    handle->tx_dma_active = 0U;
    handle->tx_state = ADEV_USART_TX_IDLE;
    handle->rx_state = ADEV_USART_RX_IDLE;
    handle->rx_mutex = NULL;
    handle->tx_mutex = NULL;
    handle->tx_dma_active = 0U;
    handle->tx_state = ADEV_USART_TX_IDLE;
    handle->rx_state = ADEV_USART_RX_IDLE;
    handle->rx_mutex = NULL;
    handle->tx_mutex = NULL;
    handle->rx_wait_object = NULL;
    handle->tx_wait_object = NULL;
    handle->event_callback = NULL;
    handle->event_argument = NULL;
    handle->idle_event_count = 0U;
    handle->rx_overflow = A_FALSE;
    handle->tx_error = A_STATUS_OK;
    handle->tx_error = A_STATUS_OK;
}

aStatus_t aDevUsartInit(const aDevUsartConfig_t *config,
                        aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if ((config == NULL) || (handle == NULL) ||
        !mode_is_valid(config->mode)) {
        return A_STATUS_INVALID_PARAM;
    }

    status = aDrvUsartInitStatic(&config->drv_config, &handle->drv_handle);
    if (status != A_STATUS_OK) {
        return status;
    }
    handle->mode = config->mode;

    status = mutexes_create(handle);
    if (status == A_STATUS_OK) {
        status = wait_objects_create(handle);
    }
    status = mutexes_create(handle);
    if (status == A_STATUS_OK) {
        status = wait_objects_create(handle);
    }
    if (status == A_STATUS_OK) {
        status = tx_mode_init(handle, config);
    }
    if (status == A_STATUS_OK) {
        status = rx_mode_init(handle, config);
    }

    if (status != A_STATUS_OK) {
        (void)aDrvUsartDeInitStatic(&handle->drv_handle);
        wait_objects_destroy(handle);
        mutexes_destroy(handle);
        mutexes_destroy(handle);
        aDevUsartHandleStructInit(handle);
    }
    return status;
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
        (handle->rx_state != ADEV_USART_RX_IDLE)) {
        return A_STATUS_BUSY;
    }

    if (!handle->drv_handle.initialized) {
        return A_STATUS_NOT_READY;
    }
    if ((handle->tx_state != ADEV_USART_TX_IDLE) ||
        (handle->rx_state != ADEV_USART_RX_IDLE)) {
        return A_STATUS_BUSY;
    }

    status = aDrvUsartDeInitStatic(&handle->drv_handle);
    if (status == A_STATUS_OK) {
        wait_objects_destroy(handle);
        mutexes_destroy(handle);
        mutexes_destroy(handle);
        aDevUsartHandleStructInit(handle);
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

    aDrvUsartDisableInterrupt(&handle->drv_handle);
    handle->event_argument = argument;
    handle->event_callback = callback;
    aDrvUsartEnableInterrupt(&handle->drv_handle);
    return A_STATUS_OK;
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

    aDrvUsartDisableInterrupt(&handle->drv_handle);
    handle->event_callback = NULL;
    handle->event_argument = NULL;
    aDrvUsartEnableInterrupt(&handle->drv_handle);
    return A_STATUS_OK;
}

static aStatus_t wait_for_event(void *wait_object,
                                const aTimepoint_t *end)
{
    return aOSWaitObjectWait(
        wait_object, aTimepointRemaining(end, aOSGetUptimeMs()));
}

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
                             size_t buffer_size, const aTimepoint_t *end,
                             aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < buffer_size) {
        const aStatus_t status = aDrvUsartTryReadByte(
            &handle->drv_handle, (uint8_t *)buffer + count);

        if (status == A_STATUS_OK) {
            ++count;
        } else if (status != A_STATUS_BUSY) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithStatus(status);
        } else if (aOSPollWaitExpired(end)) {
        } else if (aOSPollWaitExpired(end)) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithTimeout(original_timeout);
                               : aOSFailWithTimeout(original_timeout);
        }
    }
    return (aSSize_t)count;
}

static aSSize_t buffered_read(aDevUsartHandle_t *handle, void *buffer,
                              size_t buffer_size,
                              const aTimepoint_t *end,
                              aTimeout_t original_timeout)
                              size_t buffer_size,
                              const aTimepoint_t *end,
                              aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < buffer_size) {
        aBool_t available;
        aStatus_t status = A_STATUS_OK;

        aDrvUsartDisableInterrupt(&handle->drv_handle);
        if ((handle->mode & ADEV_USART_RX_MASK) ==
            ADEV_USART_RX_DMA_CIRCULAR) {
            status = dma_rx_commit(handle);
        }
        available = handle->rx_count != 0U;
        if (available) {
            ((uint8_t *)buffer)[count] = handle->rx_buffer[handle->rx_tail];
            handle->rx_tail = (handle->rx_tail + 1U) % handle->rx_buffer_size;
            --handle->rx_count;
        }
        aDrvUsartEnableInterrupt(&handle->drv_handle);

        if (available) {
            ++count;
        } else if (status != A_STATUS_OK) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithStatus(status);
        } else if (handle->rx_wait_object != NULL) {
            status = wait_for_event(handle->rx_wait_object, end);
            status = wait_for_event(handle->rx_wait_object, end);
            if (status != A_STATUS_OK) {
                return count != 0U ? (aSSize_t)count
                                   : fail_with_wait_status(
                                         status, original_timeout);
                                   : fail_with_wait_status(
                                         status, original_timeout);
            }
        } else if (aOSPollWaitExpired(end)) {
        } else if (aOSPollWaitExpired(end)) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithTimeout(original_timeout);
                               : aOSFailWithTimeout(original_timeout);
        }
    }
    return (aSSize_t)count;
}

aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *buffer,
                       size_t buffer_size, aTimeout_t timeout)
{
    aTimepoint_t end;
    aStatus_t status;
    aSSize_t result;

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
    if (handle->rx_state != ADEV_USART_RX_IDLE) {
        (void)aOSMutexUnlock(handle->rx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }

    handle->rx_state = ADEV_USART_RX_STREAM;
    result = ((handle->mode & ADEV_USART_RX_MASK) !=
              ADEV_USART_RX_POLLING)
                 ? buffered_read(handle, buffer, buffer_size, &end,
                                 timeout)
                 : polling_read(handle, buffer, buffer_size, &end,
                                timeout);
    handle->rx_state = ADEV_USART_RX_IDLE;
    (void)aOSMutexUnlock(handle->rx_mutex);
    return result;
    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(
        handle->rx_mutex,
        aTimepointRemaining(&end, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) {
        return fail_with_wait_status(status, timeout);
    }
    if (handle->rx_state != ADEV_USART_RX_IDLE) {
        (void)aOSMutexUnlock(handle->rx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }

    handle->rx_state = ADEV_USART_RX_STREAM;
    result = ((handle->mode & ADEV_USART_RX_MASK) !=
              ADEV_USART_RX_POLLING)
                 ? buffered_read(handle, buffer, buffer_size, &end,
                                 timeout)
                 : polling_read(handle, buffer, buffer_size, &end,
                                timeout);
    handle->rx_state = ADEV_USART_RX_IDLE;
    (void)aOSMutexUnlock(handle->rx_mutex);
    return result;
}

static aSSize_t polling_write(aDevUsartHandle_t *handle, const void *data,
                              size_t data_size,
                              const aTimepoint_t *end,
                              aTimeout_t original_timeout)
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
        } else if (aOSPollWaitExpired(end)) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithTimeout(original_timeout);
                               : aOSFailWithTimeout(original_timeout);
        }
    }
    return (aSSize_t)count;
}

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
    status = aDrvUsartAsyncTxStart(
        &handle->drv_handle, &handle->tx_buffer[handle->tx_tail],
        contiguous, &started);
    if (status != A_STATUS_OK) {
        return status;
    }

    handle->tx_dma_active = started;
    status = aDrvUsartSetInterruptEnabled(
        &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
    if (status != A_STATUS_OK) {
        (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
        handle->tx_dma_active = 0U;
    }
    return status;
}

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

static aSSize_t dma_buffered_write(aDevUsartHandle_t *handle,
                                   const void *data, size_t data_size,
                                   const aTimepoint_t *end,
                                   aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < data_size) {
        size_t accepted = 0U;
        aStatus_t status;

        aDrvUsartDisableInterrupt(&handle->drv_handle);
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
        aDrvUsartEnableInterrupt(&handle->drv_handle);

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

static aSSize_t interrupt_write(aDevUsartHandle_t *handle, const void *data,
                                size_t data_size,
                                const aTimepoint_t *end,
                                aTimeout_t original_timeout)
                                size_t data_size,
                                const aTimepoint_t *end,
                                aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < data_size) {
        aBool_t space_available;
        aStatus_t status = A_STATUS_OK;

        aDrvUsartDisableInterrupt(&handle->drv_handle);
        space_available = handle->tx_count < handle->tx_buffer_size;
        if (space_available) {
            handle->tx_buffer[handle->tx_head] = ((const uint8_t *)data)[count];
            handle->tx_head = (handle->tx_head + 1U) % handle->tx_buffer_size;
            ++handle->tx_count;
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TXE, A_TRUE);
        }
        aDrvUsartEnableInterrupt(&handle->drv_handle);

        if (space_available) {
            ++count;
        } else {
            status = wait_for_event(handle->tx_wait_object, end);
            status = wait_for_event(handle->tx_wait_object, end);
            if (status != A_STATUS_OK) {
                return count != 0U ? (aSSize_t)count
                                   : fail_with_wait_status(
                                         status, original_timeout);
                                   : fail_with_wait_status(
                                         status, original_timeout);
            }
        }
    }
    return (aSSize_t)count;
}

aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *data,
                        size_t data_size, aTimeout_t timeout)
{
    aTimepoint_t end;
    aStatus_t status;
    aSSize_t result;

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
    if (handle->tx_state != ADEV_USART_TX_IDLE) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }

    handle->tx_state = ADEV_USART_TX_STREAM;
    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(
        handle->tx_mutex,
        aTimepointRemaining(&end, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) {
        return fail_with_wait_status(status, timeout);
    }
    if (handle->tx_state != ADEV_USART_TX_IDLE) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }

    handle->tx_state = ADEV_USART_TX_STREAM;
    switch (handle->mode & ADEV_USART_TX_MASK) {
    case ADEV_USART_TX_DMA_BUFFERED:
        result = dma_buffered_write(handle, data, data_size, &end,
                                    timeout);
        break;
    case ADEV_USART_TX_DMA_BUFFERED:
        result = dma_buffered_write(handle, data, data_size, &end,
                                    timeout);
        break;
    case ADEV_USART_TX_INTERRUPT_BUFFERED:
        result = interrupt_write(handle, data, data_size, &end,
                                 timeout);
        break;
        result = interrupt_write(handle, data, data_size, &end,
                                 timeout);
        break;
    case ADEV_USART_TX_POLLING:
    default:
        result = polling_write(handle, data, data_size, &end, timeout);
        break;
    }
    handle->tx_state = ADEV_USART_TX_IDLE;
    (void)aOSMutexUnlock(handle->tx_mutex);
    return result;
}

aBool_t aDevUsartIsSupported(const aDevUsartHandle_t *handle,
                             aDevUsartCapability_t capability)
{
    if ((handle == NULL) || !handle->drv_handle.initialized) {
        return A_FALSE;
    }

    switch (capability) {
    case ADEV_USART_CAP_TX_DIRECT:
        return aDrvUsartAsyncTxIsSupported(&handle->drv_handle);
    case ADEV_USART_CAP_RX_DIRECT:
        return aDrvUsartAsyncRxIsSupported(&handle->drv_handle);
    default:
        return A_FALSE;
    }
}

static void direct_tx_interrupts_disable(aDevUsartHandle_t *handle)
{
    const aDevUsartMode_t tx_mode = handle->mode & ADEV_USART_TX_MASK;

    if (tx_mode == ADEV_USART_TX_INTERRUPT_BUFFERED) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TXE, A_FALSE);
    }
    if ((tx_mode == ADEV_USART_TX_INTERRUPT_BUFFERED) ||
        (tx_mode == ADEV_USART_TX_DMA_BUFFERED)) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
    }
}

aSSize_t aDevUsartWriteDirect(aDevUsartHandle_t *handle,
                              const void *data, size_t data_size,
                              aTimeout_t timeout)
{
    aTimepoint_t end;
    aStatus_t status;
    size_t count = 0U;

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
    if (!aDevUsartIsSupported(handle, ADEV_USART_CAP_TX_DIRECT)) {
        return aOSFailWithStatus(A_STATUS_UNSUPPORTED);
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(
        handle->tx_mutex,
        aTimepointRemaining(&end, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) {
        return fail_with_wait_status(status, timeout);
    }

    aDrvUsartDisableInterrupt(&handle->drv_handle);
    if ((handle->tx_state != ADEV_USART_TX_IDLE) ||
        (handle->tx_count != 0U) ||
        (handle->tx_dma_active != 0U)) {
        aDrvUsartEnableInterrupt(&handle->drv_handle);
        (void)aOSMutexUnlock(handle->tx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }
    handle->tx_state = ADEV_USART_TX_DIRECT;
    direct_tx_interrupts_disable(handle);
    aDrvUsartEnableInterrupt(&handle->drv_handle);

    while (count < data_size) {
        size_t started = 0U;
        size_t remaining;

        status = aDrvUsartAsyncTxStart(
            &handle->drv_handle, (const uint8_t *)data + count,
            data_size - count, &started);
        if (status != A_STATUS_OK) {
            break;
        }

        for (;;) {
            status = aDrvUsartAsyncTxGetRemaining(
                &handle->drv_handle, &remaining);
            if (status != A_STATUS_OK) {
                count += started - remaining;
                break;
            }
            if (remaining == 0U) {
                count += started;
                break;
            }
            if (aOSPollWaitExpired(&end)) {
                (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
                count += started - remaining;
                status = A_STATUS_TIMEOUT;
                break;
            }
        }
        if (status != A_STATUS_OK) {
            break;
        }
    }

    handle->tx_state = ADEV_USART_TX_IDLE;
    (void)aOSMutexUnlock(handle->tx_mutex);
    if (count != 0U) {
        return (aSSize_t)count;
    }
    return status == A_STATUS_TIMEOUT
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
}

static void direct_rx_interrupt_set(aDevUsartHandle_t *handle,
                                    aBool_t enabled)
{
    if ((handle->mode & ADEV_USART_RX_MASK) ==
        ADEV_USART_RX_INTERRUPT_BUFFERED) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_RXNE, enabled);
    }
}

aSSize_t aDevUsartReadDirect(aDevUsartHandle_t *handle, void *buffer,
                             size_t buffer_size, aTimeout_t timeout)
{
    aTimepoint_t end;
    aStatus_t status;
    size_t count = 0U;

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
    if (!aDevUsartIsSupported(handle, ADEV_USART_CAP_RX_DIRECT)) {
        return aOSFailWithStatus(A_STATUS_UNSUPPORTED);
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(
        handle->rx_mutex,
        aTimepointRemaining(&end, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) {
        return fail_with_wait_status(status, timeout);
    }

    aDrvUsartDisableInterrupt(&handle->drv_handle);
    if ((handle->rx_state != ADEV_USART_RX_IDLE) ||
        (handle->rx_count != 0U) ||
        ((handle->mode & ADEV_USART_RX_MASK) ==
         ADEV_USART_RX_DMA_CIRCULAR)) {
        aDrvUsartEnableInterrupt(&handle->drv_handle);
        (void)aOSMutexUnlock(handle->rx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }
    handle->rx_state = ADEV_USART_RX_DIRECT;
    direct_rx_interrupt_set(handle, A_FALSE);
    aDrvUsartEnableInterrupt(&handle->drv_handle);

    while (count < buffer_size) {
        size_t transfer_size = buffer_size - count;
        size_t remaining;
        size_t received = 0U;

        if (transfer_size > 65535U) {
            transfer_size = 65535U;
        }
        status = aDrvUsartAsyncRxStart(
            &handle->drv_handle, (uint8_t *)buffer + count,
            transfer_size);
        if (status != A_STATUS_OK) {
            break;
        }

        for (;;) {
            status = aDrvUsartAsyncRxGetRemaining(
                &handle->drv_handle, &remaining);
            if ((status != A_STATUS_OK) || (remaining == 0U) ||
                aOSPollWaitExpired(&end)) {
                const aBool_t expired =
                    (status == A_STATUS_OK) && (remaining != 0U);

                if (aDrvUsartAsyncRxStop(
                        &handle->drv_handle, &received) != A_STATUS_OK) {
                    (void)aDrvUsartAsyncRxAbort(&handle->drv_handle);
                    if (status == A_STATUS_OK) {
                        status = A_STATUS_ERROR;
                    }
                }
                count += received;
                if (expired) {
                    status = A_STATUS_TIMEOUT;
                }
                break;
            }
        }
        if (status != A_STATUS_OK) {
            break;
        }
    }

    direct_rx_interrupt_set(handle, A_TRUE);
    handle->rx_state = ADEV_USART_RX_IDLE;
    (void)aOSMutexUnlock(handle->rx_mutex);
    if (count != 0U) {
        return (aSSize_t)count;
    }
    return status == A_STATUS_TIMEOUT
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
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
            (((handle->mode & ADEV_USART_TX_MASK) ==
              ADEV_USART_TX_POLLING) ||
             ((handle->tx_count == 0U) &&
              (handle->tx_dma_active == 0U)))) {
            return A_STATUS_OK;
        }
        if (((handle->mode & ADEV_USART_TX_MASK) ==
             ADEV_USART_TX_INTERRUPT_BUFFERED) ||
            ((handle->mode & ADEV_USART_TX_MASK) ==
             ADEV_USART_TX_DMA_BUFFERED)) {
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
            const aStatus_t wait_status = wait_for_event(
                handle->tx_wait_object, end);
                handle->tx_wait_object, end);

            if (wait_status != A_STATUS_OK) {
                if ((wait_status == A_STATUS_BUSY) ||
                    (wait_status == A_STATUS_TIMEOUT)) {
                    return ((original_timeout.type ==
                             A_TIMEOUT_TYPE_RELATIVE) &&
                            (original_timeout.milliseconds == 0U))
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
        if (aOSPollWaitExpired(end)) {
            return original_timeout.milliseconds == 0U
                       ? A_STATUS_BUSY
                       : A_STATUS_TIMEOUT;
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
    if (handle->tx_state != ADEV_USART_TX_IDLE) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return A_STATUS_BUSY;
    }

    handle->tx_state = ADEV_USART_TX_STREAM;
    status = wait_transmit_complete_locked(handle, &end, timeout);
    handle->tx_state = ADEV_USART_TX_IDLE;
    (void)aOSMutexUnlock(handle->tx_mutex);
    return status;
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
    if (handle->tx_state != ADEV_USART_TX_IDLE) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return A_STATUS_BUSY;
    }

    handle->tx_state = ADEV_USART_TX_STREAM;
    status = wait_transmit_complete_locked(handle, &end, timeout);
    handle->tx_state = ADEV_USART_TX_IDLE;
    (void)aOSMutexUnlock(handle->tx_mutex);
    return status;
}

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
