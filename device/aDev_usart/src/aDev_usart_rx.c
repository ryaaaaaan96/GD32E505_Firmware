#include "aDev_usart_internal.h"

#include <limits.h>
#include <string.h>

#if ADEV_USART_ASYNC_ENABLE
static void async_rx_dispatch(aDevUsartHandle_t *handle);
#endif

#if ADEV_USART_INTERRUPT_ENABLE
static void irq_receive(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    uint8_t data;

    if (aDrvUsartTryReadByte(&handle->drv_handle, &data) != A_STATUS_OK) {
        return;
    }
    if (handle->rx_count >= handle->rx_buffer_size) {
        handle->rx_overflow = A_TRUE;
#if ADEV_USART_ASYNC_ENABLE
        async_rx_dispatch(handle);
#endif
        return;
    }

    handle->rx_buffer[handle->rx_head] = data;
    handle->rx_head = (handle->rx_head + 1U) % handle->rx_buffer_size;
    ++handle->rx_count;
#if ADEV_USART_ASYNC_ENABLE
    async_rx_dispatch(handle);
#endif
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
}

#endif

#if ADEV_USART_INTERRUPT_ENABLE
static void irq_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->idle_event_count;
#if ADEV_USART_DMA_BACKEND_ENABLE
    if ((handle->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_DMA_BUFFERED)
        aDevUsartRxDmaNotifyFromISR(handle);
#endif
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
}

#endif

#if ADEV_USART_DMA_BACKEND_ENABLE && ADEV_USART_INTERRUPT_ENABLE
static void rx_dma_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->idle_event_count;
    aDevUsartRxDmaNotifyFromISR(handle);
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
}

#endif

#if ADEV_USART_INTERRUPT_ENABLE
static aStatus_t rx_idle_detection_enable(
    aDevUsartHandle_t *handle, const aDevUsartConfig_t *config)
{
    aDrvInterruptCallback_t callback = irq_idle;

    if (!aDrvUsartInterruptIsSupported()) {
        return A_STATUS_UNSUPPORTED;
    }

    return aDevUsartRegisterIrqCallback(
        handle, ADRV_USART_EXTI_IDLE, callback,
        config->interrupt_priority, A_TRUE);
}

#endif

aStatus_t aDevUsartRxModeInit(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config)
{
    aStatus_t status;
    (void)handle;

    switch (config->mode & ADEV_USART_RX_MASK) {
    case ADEV_USART_RX_POLLING:
        status = A_STATUS_OK;
        break;
#if ADEV_USART_INTERRUPT_ENABLE
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
        status = aDevUsartRegisterIrqCallback(
            handle, ADRV_USART_EXTI_RXNE, irq_receive,
            config->interrupt_priority, A_TRUE);
        break;
#endif

#if ADEV_USART_DMA_BACKEND_ENABLE
    case ADEV_USART_RX_DMA_BUFFERED:
        if (!ADEV_USART_DMA_BACKEND_ENABLE ||
            !aDrvUsartAsyncRxIsSupported(&handle->drv_handle)) {
            return A_STATUS_UNSUPPORTED;
        }
        /* No ring: DMA is reserved for explicit Direct requests. */
        if (config->rx_buffer == NULL && config->rx_buffer_size == 0U) {
            if (config->mode & ADEV_USART_OPTION_RX_IDLE)
                return A_STATUS_INVALID_PARAM;
            return A_STATUS_OK;
        }
        if ((config->rx_buffer == NULL) || (config->rx_buffer_size < 2U) ||
            (config->rx_buffer_size > 65535U)) {
            return A_STATUS_INVALID_PARAM;
        }
        if ((config->mode & ADEV_USART_OPTION_RX_IDLE) != 0U &&
            !aDrvUsartInterruptIsSupported()) {
            return A_STATUS_UNSUPPORTED;
        }
        handle->rx_buffer = config->rx_buffer;
        handle->rx_buffer_size = config->rx_buffer_size;
        status = aDrvUsartAsyncRxCircularStart(
            &handle->drv_handle, handle->rx_buffer, handle->rx_buffer_size,
            config->interrupt_priority, aDevUsartRxDmaComplete, handle);
        if (status == A_STATUS_OK) {
            handle->rx_dma_active = A_TRUE;
        }
        break;
#endif

    default:
        return A_STATUS_INVALID_PARAM;
    }

#if ADEV_USART_INTERRUPT_ENABLE
    if ((status == A_STATUS_OK) &&
        ((config->mode & ADEV_USART_OPTION_RX_IDLE) != 0U)) {
#if ADEV_USART_DMA_BACKEND_ENABLE
        if ((config->mode & ADEV_USART_RX_MASK) ==
            ADEV_USART_RX_DMA_BUFFERED) {
            status = aDevUsartRegisterIrqCallback(
                handle, ADRV_USART_EXTI_IDLE, rx_dma_idle,
                config->interrupt_priority, A_TRUE);
        } else
#endif
        {
            status = rx_idle_detection_enable(handle, config);
        }
    }
#endif

    return status;
}

/* Stream reads. */
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

#if ADEV_USART_INTERRUPT_ENABLE
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

#if ADEV_USART_DMA_BACKEND_ENABLE
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
    if (handle->rx_state != ADEV_USART_RX_IDLE) {
        (void)aOSMutexUnlock(handle->rx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }

    handle->rx_state = ADEV_USART_RX_STREAM;
#if ADEV_USART_DMA_BACKEND_ENABLE
    if ((handle->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_DMA_BUFFERED) {
        if (!handle->rx_dma_active) {
            handle->rx_state = ADEV_USART_RX_IDLE;
            (void)aOSMutexUnlock(handle->rx_mutex);
            return aOSFailWithStatus(A_STATUS_UNSUPPORTED);
        }
        result = dma_buffered_read(handle, buffer, buffer_size, &end, timeout);
    } else
#endif
#if ADEV_USART_INTERRUPT_ENABLE
    if ((handle->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_INTERRUPT_BUFFERED) {
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


#if ADEV_USART_DMA_BACKEND_ENABLE
/* Shared circular DMA buffer. */
/* Updates the DMA producer count and preserves the newest ring contents. */
aStatus_t aDevUsartDmaRxRefresh(aDevUsartHandle_t *handle)
{
    size_t produced = handle->rx_dma_produced;
    const aStatus_t status = aDrvUsartAsyncRxGetReceivedCount(
        &handle->drv_handle, &produced);
    if (status != A_STATUS_OK) return status;

    handle->rx_dma_produced = produced;
    if (produced - handle->rx_dma_consumed > handle->rx_buffer_size) {
        handle->rx_dma_consumed = produced - handle->rx_buffer_size;
        handle->rx_overflow = A_TRUE;
        handle->rx_error = A_STATUS_ERROR;
    }
    return A_STATUS_OK;
}


/* CPU locks do not stop DMA. Copy then validate the producer against the
 * original cursor before publishing any bytes. Never lend the live DMA ring.
 * Caller holds rx_mutex; DMA IRQ must remain enabled during this operation. */
aStatus_t aDevUsartDmaRxCopy(aDevUsartHandle_t *handle, void *buffer,
                             size_t capacity, size_t *copied)
{
    *copied = 0U;
    aStatus_t status = aDevUsartDmaRxRefresh(handle);
    if (status != A_STATUS_OK) return status;
    const size_t start = handle->rx_dma_consumed;
    size_t length = handle->rx_dma_produced - start;
    if (length > capacity) length = capacity;
    const size_t offset = start % handle->rx_buffer_size;
    if (length > handle->rx_buffer_size - offset)
        length = handle->rx_buffer_size - offset;
    if (length == 0U) return A_STATUS_OK;
    memcpy(buffer, handle->rx_buffer + offset, length);
    atomic_thread_fence(memory_order_seq_cst);
    status = aDevUsartDmaRxRefresh(handle);
    if (status != A_STATUS_OK) return status;
    if (handle->rx_dma_produced - start > handle->rx_buffer_size) {
        /* The copied span may be torn. Report no bytes, keep overflow latched. */
        return A_STATUS_ERROR;
    }
    handle->rx_dma_consumed = start + length;
    *copied = length;
    return A_STATUS_OK;
}

void aDevUsartRxDmaNotifyFromISR(aDevUsartHandle_t *handle)
{
    if (!handle->rx_dma_active) return;
#if ADEV_USART_ASYNC_ENABLE
    async_rx_dispatch(handle);
#endif
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
}

void aDevUsartRxDmaComplete(void *argument)
{
    aDevUsartRxDmaNotifyFromISR(argument);
}

#endif /* DMA buffer backend */

#if ADEV_USART_DIRECT_ENABLE && ADEV_USART_DMA_BACKEND_ENABLE
/* DMA implementation of synchronous user-buffer RX. */
static void direct_rx_complete(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
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

static aSSize_t dma_read_direct(aDevUsartHandle_t *handle, void *buffer,
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
    if (buffer_size == 0U) return 0;
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
        handle->rx_dispatching ||
        handle->rx_dma_active ||
        (handle->rx_count != 0U)) {
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

        if (transfer_size > 65535U) transfer_size = 65535U;
        status = aDrvUsartRxDmaStart(
            &handle->drv_handle, (uint8_t *)buffer + count, transfer_size,
            handle->interrupt_priority, direct_rx_complete, handle);
        if (status != A_STATUS_OK) break;

        for (;;) {
            status = aDrvUsartAsyncRxGetRemaining(
                &handle->drv_handle, &remaining);
            if (status == A_STATUS_OK && remaining != 0U)
                status = direct_wait(handle->rx_wait_object, &end, A_FALSE);
            if ((status != A_STATUS_OK) || (remaining == 0U) ||
                aTimepointExpired(&end, aOSGetUptimeMs())) {
                const aBool_t expired =
                    (status == A_STATUS_OK) && (remaining != 0U);

                const aStatus_t stop_status = aDrvUsartAsyncRxStop(
                    &handle->drv_handle, &received);
                if (stop_status != A_STATUS_OK) {
                    (void)aDrvUsartAsyncRxAbort(&handle->drv_handle);
                    if (status == A_STATUS_OK) status = stop_status;
                }
                if (received > transfer_size) {
                    status = A_STATUS_ERROR;
                } else {
                    count += received;
                }
                if (expired && status == A_STATUS_OK) status = A_STATUS_TIMEOUT;
                if (status == A_STATUS_OK && received == 0U) status = A_STATUS_ERROR;
                break;
            }
        }
        if (status != A_STATUS_OK) break;
        if (count < buffer_size && aTimepointExpired(&end, aOSGetUptimeMs())) {
            status = A_STATUS_TIMEOUT;
            break;
        }
    }

    direct_rx_interrupt_set(handle, A_TRUE);
    handle->rx_state = ADEV_USART_RX_IDLE;
    (void)aOSMutexUnlock(handle->rx_mutex);
    if (count != 0U) return (aSSize_t)count;
    return status == A_STATUS_TIMEOUT
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
}

#endif

#if ADEV_USART_DIRECT_ENABLE
aSSize_t aDevUsartReadDirect(aDevUsartHandle_t *handle, void *buffer,
                             size_t buffer_size, aTimeout_t timeout)
{
    if (handle == NULL || !aTimeoutIsValid(timeout) ||
        buffer_size > (size_t)PTRDIFF_MAX || (buffer == NULL && buffer_size != 0U))
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    if (!handle->drv_handle.initialized)
        return aOSFailWithStatus(A_STATUS_NOT_READY);
    if (buffer_size == 0U) return 0;
    if (handle->rx_state != ADEV_USART_RX_IDLE || handle->rx_dispatching)
        return aOSFailWithStatus(A_STATUS_BUSY);
#if ADEV_USART_DMA_BACKEND_ENABLE
    if ((handle->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_DMA_BUFFERED)
        return dma_read_direct(handle, buffer, buffer_size, timeout);
#endif
    if ((handle->mode & ADEV_USART_RX_MASK) != ADEV_USART_RX_POLLING)
        return aOSFailWithStatus(A_STATUS_UNSUPPORTED);

    const aTimepoint_t end = aTimepointCalc(timeout, aOSGetUptimeMs());
    aStatus_t status = aOSMutexLock(handle->rx_mutex,
        aTimepointRemaining(&end, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) return fail_with_wait_status(status, timeout);
    aOSCriticalEnter();
    if (handle->rx_state != ADEV_USART_RX_IDLE || handle->rx_dispatching || handle->rx_dma_active ||
        handle->rx_count != 0U) {
        aOSCriticalExit();
        (void)aOSMutexUnlock(handle->rx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }
    handle->rx_state = ADEV_USART_RX_DIRECT;
#if ADEV_USART_INTERRUPT_ENABLE
    if ((handle->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_INTERRUPT_BUFFERED)
        (void)aDrvUsartSetInterruptEnabled(&handle->drv_handle,
                                          ADRV_USART_EXTI_RXNE, A_FALSE);
    if (handle->mode & ADEV_USART_OPTION_RX_IDLE)
        (void)aDrvUsartSetInterruptEnabled(&handle->drv_handle,
                                          ADRV_USART_EXTI_IDLE, A_FALSE);
#endif
    aOSCriticalExit();

    size_t count = 0U;
    while (count < buffer_size) {
        status = aDrvUsartTryReadByte(&handle->drv_handle, (uint8_t *)buffer + count);
        if (status == A_STATUS_OK) {
            ++count;
        } else if (status != A_STATUS_BUSY) {
            break;
        } else if (aOSPollWaitExpired(&end)) {
            status = A_STATUS_TIMEOUT;
            break;
        }
    }
    aOSCriticalEnter();
#if ADEV_USART_INTERRUPT_ENABLE
    if ((handle->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_INTERRUPT_BUFFERED)
        (void)aDrvUsartSetInterruptEnabled(&handle->drv_handle,
                                          ADRV_USART_EXTI_RXNE, A_TRUE);
    if (handle->mode & ADEV_USART_OPTION_RX_IDLE)
        (void)aDrvUsartSetInterruptEnabled(&handle->drv_handle,
                                          ADRV_USART_EXTI_IDLE, A_TRUE);
#endif
    handle->rx_state = ADEV_USART_RX_IDLE;
    aOSCriticalExit();
    (void)aOSMutexUnlock(handle->rx_mutex);
    if (count != 0U) return (aSSize_t)count;
    return status == A_STATUS_TIMEOUT ? aOSFailWithTimeout(timeout)
                                      : aOSFailWithStatus(status);
}
#endif

#if ADEV_USART_ASYNC_ENABLE
/* RX callback borrows the shared ring. No request heap, queue or worker.
 * Called from RX IRQ paths only; reentrant IRQs leave progress for a later IRQ. */
static void async_rx_dispatch(aDevUsartHandle_t *handle)
{
    aOSCriticalState_t key = aOSCriticalEnterFromISR();
    if (handle->rx_state != ADEV_USART_RX_ASYNC || handle->rx_dispatching) {
        aOSCriticalExitFromISR(key);
        return;
    }
    handle->rx_dispatching = A_TRUE;
    aDevUsartRxCallback_t callback = handle->rx_callback;
    void *argument = handle->rx_callback_argument;
    aOSCriticalExitFromISR(key);

    aStatus_t status = A_STATUS_OK;
#if ADEV_USART_DMA_BACKEND_ENABLE
    if (handle->rx_dma_active) status = aDevUsartDmaRxRefresh(handle);
#endif
    if (handle->rx_overflow) status = A_STATUS_ERROR;
    /* Snapshot a bounded amount: continuous arrival cannot keep this ISR forever. */
    size_t available = handle->rx_dma_active
        ? handle->rx_dma_produced - handle->rx_dma_consumed : handle->rx_count;
    for (unsigned span = 0U; status == A_STATUS_OK && available && span < 2U; ++span) {
        const size_t start = handle->rx_dma_active
            ? handle->rx_dma_consumed : handle->rx_tail;
        const size_t offset = start % handle->rx_buffer_size;
        size_t length = handle->rx_buffer_size - offset;
        if (length > available) length = available;
        const aDevUsartRxEvent_t event = {
            .type = ADEV_USART_RX_EVENT_DATA_READY,
            .buffer = handle->rx_buffer + offset,
            .offset = 0U, .length = length, .status = A_STATUS_OK,
        };
        callback(handle, &event, argument);
#if ADEV_USART_DMA_BACKEND_ENABLE
        if (handle->rx_dma_active) {
            status = aDevUsartDmaRxRefresh(handle);
            if (status == A_STATUS_OK &&
                handle->rx_dma_produced - start > handle->rx_buffer_size) {
                status = A_STATUS_ERROR;
            }
            if (status == A_STATUS_OK) handle->rx_dma_consumed = start + length;
        } else
#endif
        {
            key = aOSCriticalEnterFromISR();
            handle->rx_tail = (start + length) % handle->rx_buffer_size;
            handle->rx_count -= length;
            aOSCriticalExitFromISR(key);
        }
        available -= length;
        if (handle->rx_overflow) status = A_STATUS_ERROR;
    }
    if (status != A_STATUS_OK) {
        handle->rx_error = status;
        const aDevUsartRxEvent_t event = {
            .type = ADEV_USART_RX_EVENT_ERROR, .status = status,
        };
        callback(handle, &event, argument);
    }
    key = aOSCriticalEnterFromISR();
    if (status != A_STATUS_OK) {
        handle->rx_callback = NULL;
        handle->rx_callback_argument = NULL;
        handle->rx_state = ADEV_USART_RX_IDLE;
    }
    handle->rx_dispatching = A_FALSE;
    aOSCriticalExitFromISR(key);
}

aStatus_t aDevUsartReadAsync(aDevUsartHandle_t *handle,
                            const aDevUsartReadRequest_t *request)
{
    if (handle == NULL || request == NULL || request->callback == NULL)
        return A_STATUS_INVALID_PARAM;
    if (!handle->drv_handle.initialized) return A_STATUS_NOT_READY;
    if ((handle->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_POLLING ||
        handle->rx_buffer == NULL) return A_STATUS_UNSUPPORTED;
    if (handle->rx_dispatching) return A_STATUS_BUSY;
    aStatus_t status = aOSMutexLock(handle->rx_mutex, A_TIMEOUT_NO_WAIT);
    if (status != A_STATUS_OK) return status;
    aOSCriticalEnter();
    if (handle->rx_state != ADEV_USART_RX_IDLE || handle->rx_dispatching) {
        status = A_STATUS_BUSY;
    } else {
#if ADEV_USART_DMA_BACKEND_ENABLE
        if (handle->rx_dma_active) status = aDevUsartDmaRxRefresh(handle);
#endif
        /* Never silently discard existing stream bytes when changing consumer. */
        if (status == A_STATUS_OK && (handle->rx_count != 0U ||
            handle->rx_dma_produced != handle->rx_dma_consumed))
            status = A_STATUS_BUSY;
        if (status == A_STATUS_OK && handle->rx_error != A_STATUS_OK)
            status = handle->rx_error;
        if (status == A_STATUS_OK) {
            handle->rx_callback = request->callback;
            handle->rx_callback_argument = request->argument;
            handle->rx_state = ADEV_USART_RX_ASYNC;
        }
    }
    aOSCriticalExit();
    (void)aOSMutexUnlock(handle->rx_mutex);
    return status;
}

aStatus_t aDevUsartReadAsyncCancel(aDevUsartHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->drv_handle.initialized) return A_STATUS_NOT_READY;
    if (handle->rx_dispatching) return A_STATUS_BUSY;
    aStatus_t status = aOSMutexLock(handle->rx_mutex, A_TIMEOUT_NO_WAIT);
    if (status != A_STATUS_OK) return status;
    aOSCriticalEnter();
    if (handle->rx_state != ADEV_USART_RX_ASYNC || handle->rx_dispatching) {
        status = (handle->rx_dispatching || handle->rx_state != ADEV_USART_RX_IDLE)
            ? A_STATUS_BUSY : A_STATUS_NOT_READY;
        aOSCriticalExit();
        (void)aOSMutexUnlock(handle->rx_mutex);
        return status;
    }
    handle->rx_dispatching = A_TRUE;
    aDevUsartRxCallback_t callback = handle->rx_callback;
    void *argument = handle->rx_callback_argument;
    handle->rx_callback = NULL;
    handle->rx_callback_argument = NULL;
    aOSCriticalExit();
    (void)aOSMutexUnlock(handle->rx_mutex);
    const aDevUsartRxEvent_t event = {
        .type = ADEV_USART_RX_EVENT_CANCELLED, .status = A_STATUS_CANCELLED,
    };
    callback(handle, &event, argument);
    aOSCriticalEnter();
    handle->rx_state = ADEV_USART_RX_IDLE;
    handle->rx_dispatching = A_FALSE;
    aOSCriticalExit();
    return A_STATUS_OK;
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
