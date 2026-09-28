#include "aDev_usart_internal.h"

#include <limits.h>
#include <string.h>

#if ADEV_USART_DMA_ENABLE
static aStatus_t dma_tx_start_locked(aDevUsartHandle_t *handle);
#endif
static void rs485_complete(aDevUsartHandle_t *handle);
#if ADEV_USART_ASYNC_ENABLE
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
    handle->tx_state = ADEV_USART_TX_IDLE;
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
#if ADEV_USART_RS485_ENABLE
    const aStatus_t status = aDevUsartRS485Complete(handle);
    if (status != A_STATUS_OK) {
        handle->tx_error = status;
    }
#else
    (void)handle;
#endif
}

#if ADEV_USART_RS485_ENABLE
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

#if ADEV_USART_INTERRUPT_ENABLE
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

#if ADEV_USART_ASYNC_ENABLE
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

#if ADEV_USART_DMA_ENABLE
    if (handle->tx_state == ADEV_USART_TX_DIRECT) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
        aOSWaitObjectNotifyFromISR(handle->tx_wait_object);
        return;
    }

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

#endif

aStatus_t aDevUsartTxModeInit(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config)
{
    switch (config->mode & ADEV_USART_TX_MASK) {
    case ADEV_USART_TX_POLLING:
#if ADEV_USART_NEEDS_IRQ
        if (config->rs485.enabled || ADEV_USART_DMA_ENABLE)
            return aDevUsartRegisterIrqCallback(
                handle, ADRV_USART_EXTI_TC, irq_transmit_complete,
                config->interrupt_priority, A_FALSE);
#endif
        (void)handle;
        return A_STATUS_OK;
#if ADEV_USART_INTERRUPT_ENABLE
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

#if ADEV_USART_DMA_ENABLE
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

#if ADEV_USART_DMA_ENABLE
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
#if ADEV_USART_RS485_ENABLE
    status = aDevUsartRS485Begin(handle);
#endif
    if (status != A_STATUS_OK) {
        return status;
    }
    status = aDrvUsartAsyncTxStart(
        &handle->drv_handle, &handle->tx_buffer[handle->tx_tail],
        contiguous, &started);
    if (status != A_STATUS_OK) {
#if ADEV_USART_RS485_ENABLE
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
#if ADEV_USART_RS485_ENABLE
        aDevUsartRs485ArmComplete(handle);
#endif
    }
    return status;
}

#endif

#if ADEV_USART_DMA_ENABLE
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

#if ADEV_USART_DMA_ENABLE
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

#if ADEV_USART_INTERRUPT_ENABLE
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
#if ADEV_USART_RS485_ENABLE
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
        (handle->tx_callback != NULL)) {
        (void)aOSMutexUnlock(handle->tx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }

    handle->tx_state = ADEV_USART_TX_STREAM;
    switch (handle->mode & ADEV_USART_TX_MASK) {
#if ADEV_USART_DMA_ENABLE
    case ADEV_USART_TX_DMA_BUFFERED:
        result = dma_buffered_write(handle, data, data_size, &end,
                                    timeout);
        break;
#endif

#if ADEV_USART_INTERRUPT_ENABLE
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
#if ADEV_USART_RS485_ENABLE
            status = aDevUsartRS485Begin(handle);
#endif
        }
#if ADEV_USART_RS485_ENABLE
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
#if ADEV_USART_RS485_ENABLE
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
#if ADEV_USART_DMA_ENABLE || ADEV_USART_INTERRUPT_ENABLE
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

#if ADEV_USART_ASYNC_ENABLE
aStatus_t aDevUsartWriteAsync(aDevUsartHandle_t *handle,
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
    if (!ADEV_USART_ASYNC_ENABLE ||
        !aDrvUsartAsyncTxIsSupported(&handle->drv_handle)) {
        return A_STATUS_UNSUPPORTED;
    }
    status = aOSMutexLock(handle->tx_mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    if ((handle->tx_state != ADEV_USART_TX_IDLE) ||
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
#if ADEV_USART_RS485_ENABLE
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

aStatus_t aDevUsartWriteAsyncCancel(aDevUsartHandle_t *handle)
{
    aStatus_t status;
    size_t remaining = 0U;
    size_t transferred = 0U;

    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->drv_handle.initialized) return A_STATUS_NOT_READY;
    status = aOSMutexLock(handle->tx_mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
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

#endif

#if ADEV_USART_DMA_ENABLE
/* Synchronous zero-copy TX. */
static void direct_tx_interrupts_disable(aDevUsartHandle_t *handle)
{
    const aDevUsartMode_t tx_mode = handle->mode & ADEV_USART_TX_MASK;

    if (tx_mode == ADEV_USART_TX_INTERRUPT_BUFFERED) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TXE, A_FALSE);
    }
    if ((tx_mode == ADEV_USART_TX_INTERRUPT_BUFFERED) ||
        (tx_mode == ADEV_USART_TX_DMA_BUFFERED) || handle->rs485.enabled) {
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
        (handle->tx_callback != NULL) ||
        (handle->tx_count != 0U) || (handle->tx_dma_active != 0U)) {
        aDrvUsartEnableInterrupt(&handle->drv_handle);
        (void)aOSMutexUnlock(handle->tx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }
    handle->tx_state = ADEV_USART_TX_DIRECT;
    direct_tx_interrupts_disable(handle);
    status = handle->tx_error;
    if (status == A_STATUS_OK) {
#if ADEV_USART_RS485_ENABLE
        status = aDevUsartRS485Begin(handle);
#endif
    }
    aDrvUsartEnableInterrupt(&handle->drv_handle);

    while ((status == A_STATUS_OK) && (count < data_size)) {
        size_t started = 0U;
        size_t remaining = 0U;

        status = aDrvUsartAsyncTxStart(
            &handle->drv_handle, (const uint8_t *)data + count,
            data_size - count, &started);
        if (status != A_STATUS_OK) break;

        remaining = started;
        status = aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
        if (status != A_STATUS_OK) break;
        for (;;) {
            status = aDrvUsartAsyncTxGetRemaining(
                &handle->drv_handle, &remaining);
            if (status != A_STATUS_OK) {
                break;
            }
            if (remaining == 0U) {
                count += started;
                break;
            }
            status = direct_wait(handle->tx_wait_object, &end, A_TRUE);
            if (status != A_STATUS_OK) {
                /* Sample progress before abort clears the hardware counter. */
                size_t final_remaining = remaining;
                if (aDrvUsartAsyncTxGetRemaining(&handle->drv_handle,
                                               &final_remaining) == A_STATUS_OK &&
                    final_remaining <= started)
                    remaining = final_remaining;
                (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
                count += started - remaining;
                break;
            }
        }
        if (status != A_STATUS_OK) break;
    }

    /* Return only after DMA has stopped accessing the caller's buffer. */
    (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
    aDrvUsartDisableInterrupt(&handle->drv_handle);
    (void)aDrvUsartSetInterruptEnabled(
        &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
#if ADEV_USART_RS485_ENABLE
    aDevUsartRs485ArmComplete(handle);
#endif
    handle->tx_state = ADEV_USART_TX_IDLE;
    aDrvUsartEnableInterrupt(&handle->drv_handle);
    (void)aOSMutexUnlock(handle->tx_mutex);
    if (count != 0U) return (aSSize_t)count;
    return status == A_STATUS_TIMEOUT
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
}


#endif
