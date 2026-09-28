#include "aDev_usart_internal.h"

#include <limits.h>
#include <string.h>

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
        return;
    }

    handle->rx_buffer[handle->rx_head] = data;
    handle->rx_head = (handle->rx_head + 1U) % handle->rx_buffer_size;
    ++handle->rx_count;
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_READY);
}

#endif

#if ADEV_USART_INTERRUPT_ENABLE
static void irq_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->idle_event_count;
#if ADEV_USART_DMA_ENABLE
    if ((handle->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_DMA_BUFFERED)
        aDevUsartRxDmaNotifyFromISR(handle);
#endif
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_IDLE);
}

#endif

#if ADEV_USART_DMA_ENABLE && ADEV_USART_INTERRUPT_ENABLE
static void rx_dma_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->idle_event_count;
    aDevUsartRxDmaNotifyFromISR(handle);
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_IDLE);
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

#if ADEV_USART_DMA_ENABLE
    case ADEV_USART_RX_DMA_BUFFERED:
        if (!ADEV_USART_DMA_ENABLE ||
            !aDrvUsartAsyncRxIsSupported(&handle->drv_handle)) {
            return A_STATUS_UNSUPPORTED;
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
#if ADEV_USART_DMA_ENABLE
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

#if ADEV_USART_DMA_ENABLE
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
#if ADEV_USART_DMA_ENABLE
    if (rx_mode == ADEV_USART_RX_DMA_BUFFERED) {
        result = dma_buffered_read(handle, buffer, buffer_size, &end, timeout);
    } else
#endif
#if ADEV_USART_INTERRUPT_ENABLE
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


#if ADEV_USART_DMA_ENABLE
/* Shared circular DMA buffer. */
/* Updates the DMA producer count and preserves the newest ring contents. */
aStatus_t aDevUsartDmaRxRefresh(aDevUsartHandle_t *handle)
{
    size_t produced = handle->rx_dma_produced;
    const aBool_t overflow_was_set = handle->rx_overflow;
    const aStatus_t status = aDrvUsartAsyncRxGetReceivedCount(
        &handle->drv_handle, &produced);
    if (status != A_STATUS_OK) return status;

    handle->rx_dma_produced = produced;
    if (produced - handle->rx_dma_consumed > handle->rx_buffer_size) {
        handle->rx_dma_consumed = produced - handle->rx_buffer_size;
        handle->rx_overflow = A_TRUE;
        handle->rx_error = A_STATUS_ERROR;
        if (!overflow_was_set) {
            atomic_fetch_or_explicit(
                &handle->pending_events,
                1UL << ADEV_USART_EVENT_RX_ERROR,
                memory_order_release);
            (void)aOSWorkSubmit(&handle->event_work,
                                aDevUsartEventWork, handle);
        }
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
    (void)aOSWorkSubmitFromISR(&handle->rx_completion_work,
                               aDevUsartAsyncRxWork, handle);
#endif
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_READY);
}

void aDevUsartRxDmaComplete(void *argument)
{
    aDevUsartRxDmaNotifyFromISR(argument);
}

/* Synchronous zero-copy RX. */
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
        (handle->rx_request_head != NULL) ||
        (handle->rx_complete_head != NULL) ||
        ((handle->mode & ADEV_USART_RX_MASK) ==
         ADEV_USART_RX_DMA_BUFFERED) ||
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

#if ADEV_USART_ASYNC_ENABLE
/* One-shot asynchronous reads from the shared ring. */
static void rx_timeout(void *argument);

static void rx_timer_rearm(aDevUsartHandle_t *handle)
{
    uint32_t nearest = UINT32_MAX;
    const uint32_t now = aOSGetUptimeMs();

    for (aDevUsartReadNode_t *node = handle->rx_request_head;
         node != NULL; node = node->next) {
        if (node->request.timeout.type == A_TIMEOUT_TYPE_FOREVER) continue;
        const aTimeout_t remaining = aTimepointRemaining(&node->deadline, now);
        if (remaining.milliseconds < nearest) nearest = remaining.milliseconds;
    }
    if (nearest == UINT32_MAX) {
        aOSTimerStop(handle->rx_deadline_timer);
        return;
    }
    if (handle->rx_deadline_timer == NULL &&
        aOSTimerCreate(&handle->rx_deadline_timer, rx_timeout, handle) !=
            A_STATUS_OK) {
        return;
    }
    (void)aOSTimerStart(handle->rx_deadline_timer,
                        nearest == 0U ? 1U : nearest);
}

static void request_remove(aDevUsartHandle_t *handle,
                           aDevUsartReadNode_t *previous,
                           aDevUsartReadNode_t *node)
{
    if (previous == NULL) handle->rx_request_head = node->next;
    else previous->next = node->next;
    if (handle->rx_request_tail == node) handle->rx_request_tail = previous;
    node->next = NULL;
}

void aDevUsartAsyncRxWork(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    if (aOSMutexLock(handle->rx_mutex, A_TIMEOUT_FOREVER) != A_STATUS_OK) return;
    aStatus_t rx_status = A_STATUS_OK;
    if (handle->rx_dma_active) rx_status = aDevUsartDmaRxRefresh(handle);
    if ((rx_status != A_STATUS_OK) && (rx_status != A_STATUS_BUSY)) {
        (void)aDrvUsartAsyncRxAbort(&handle->drv_handle);
        handle->rx_dma_active = A_FALSE;
        handle->rx_error = rx_status;
    }

    /* Expired requests are removed wherever they sit in the FIFO. */
    aDevUsartReadNode_t *previous = NULL;
    aDevUsartReadNode_t *node = handle->rx_request_head;
    const uint32_t now = aOSGetUptimeMs();
    const size_t available = handle->rx_dma_produced - handle->rx_dma_consumed;
    while (node != NULL) {
        aDevUsartReadNode_t *next = node->next;
        const aBool_t no_wait_data_ready =
            (node == handle->rx_request_head) &&
            (node->request.timeout.type != A_TIMEOUT_TYPE_FOREVER) &&
            (node->request.timeout.milliseconds == 0U) &&
            (available != 0U);
        if (!handle->rx_dma_active) {
            request_remove(handle, previous, node);
            node->event = (aDevUsartRxEvent_t) {
                .type = ADEV_USART_RX_EVENT_ERROR,
                .status = handle->rx_error,
            };
            if (handle->rx_complete_tail == NULL)
                handle->rx_complete_head = node;
            else
                handle->rx_complete_tail->next = node;
            handle->rx_complete_tail = node;
        } else if (!no_wait_data_ready &&
            node->request.timeout.type != A_TIMEOUT_TYPE_FOREVER &&
            aTimepointExpired(&node->deadline, now)) {
            request_remove(handle, previous, node);
            node->event = (aDevUsartRxEvent_t) {
                .type = ADEV_USART_RX_EVENT_TIMEOUT,
                .status = A_STATUS_TIMEOUT,
            };
            if (handle->rx_complete_tail == NULL)
                handle->rx_complete_head = node;
            else
                handle->rx_complete_tail->next = node;
            handle->rx_complete_tail = node;
        } else {
            previous = node;
        }
        node = next;
    }

    /* Only the oldest live waiter claims bytes from the shared ring. */
    node = handle->rx_request_head;
    if ((node != NULL) && (available != 0U)) {
        size_t length = 0U;
        aStatus_t copy_status = aDevUsartDmaRxCopy(
            handle, node->snapshot, sizeof(node->snapshot), &length);
        request_remove(handle, NULL, node);
        node->event = (aDevUsartRxEvent_t) {
            .type = copy_status == A_STATUS_OK ? ADEV_USART_RX_EVENT_DATA_READY
                                               : ADEV_USART_RX_EVENT_ERROR,
            .buffer = copy_status == A_STATUS_OK ? node->snapshot : NULL,
            .offset = 0U, .length = length, .status = copy_status,
        };
        if (handle->rx_complete_tail == NULL)
            handle->rx_complete_head = node;
        else
            handle->rx_complete_tail->next = node;
        handle->rx_complete_tail = node;
    }
    aDevUsartReadNode_t *complete = handle->rx_complete_head;
    if (complete != NULL) {
        handle->rx_complete_head = complete->next;
        if (handle->rx_complete_head == NULL) handle->rx_complete_tail = NULL;
        complete->next = NULL;
    }
    const aBool_t more_work =
        (handle->rx_complete_head != NULL) ||
        ((handle->rx_request_head != NULL) &&
         (handle->rx_dma_produced != handle->rx_dma_consumed));
    rx_timer_rearm(handle);
    (void)aOSMutexUnlock(handle->rx_mutex);

    if (complete != NULL) {
        complete->request.callback(handle, &complete->event,
                                   complete->request.argument);
        aOSFree(complete);
    }
    if (more_work) {
        (void)aOSWorkSubmit(&handle->rx_completion_work,
                            aDevUsartAsyncRxWork, handle);
    }
}

static void rx_timeout(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    (void)aOSWorkSubmit(&handle->rx_completion_work,
                        aDevUsartAsyncRxWork, handle);
}

aStatus_t aDevUsartReadAsync(
    aDevUsartHandle_t *handle, const aDevUsartReadRequest_t *request,
    aDevUsartReadToken_t *token_out)
{
    if ((handle == NULL) || (request == NULL) || (token_out == NULL) ||
        (request->callback == NULL) || !aTimeoutIsValid(request->timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    *token_out = 0U;
    if (!handle->drv_handle.initialized) return A_STATUS_NOT_READY;
    if (!ADEV_USART_ASYNC_ENABLE || !handle->rx_dma_active ||
        ((handle->mode & ADEV_USART_RX_MASK) != ADEV_USART_RX_DMA_BUFFERED)) {
        return A_STATUS_UNSUPPORTED;
    }

    aDevUsartReadNode_t *node = aOSAlloc(sizeof(*node));
    if (node == NULL) return A_STATUS_NO_MEMORY;
    memset(node, 0, sizeof(*node));
    node->request = *request;
    node->deadline = aTimepointCalc(request->timeout, aOSGetUptimeMs());

    aStatus_t status = aOSMutexLock(handle->rx_mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) {
        aOSFree(node);
        return status;
    }
    if ((request->timeout.type != A_TIMEOUT_TYPE_FOREVER) &&
        (handle->rx_deadline_timer == NULL)) {
        status = aOSTimerCreate(&handle->rx_deadline_timer,
                                rx_timeout, handle);
        if (status != A_STATUS_OK) {
            (void)aOSMutexUnlock(handle->rx_mutex);
            aOSFree(node);
            return status;
        }
    }
    /* Reserve worker delivery while holding rx_mutex: once a request becomes
     * visible, submission can no longer fail and free a node a worker owns. */
    status = aOSWorkSubmit(&handle->rx_completion_work,
                           aDevUsartAsyncRxWork, handle);
    if (status != A_STATUS_OK && status != A_STATUS_BUSY) {
        (void)aOSMutexUnlock(handle->rx_mutex);
        aOSFree(node);
        return status;
    }
    ++handle->rx_next_token;
    if (handle->rx_next_token == 0U) ++handle->rx_next_token;
    node->token = handle->rx_next_token;
    if (handle->rx_request_tail == NULL) handle->rx_request_head = node;
    else handle->rx_request_tail->next = node;
    handle->rx_request_tail = node;
    *token_out = node->token;
    rx_timer_rearm(handle);
    (void)aOSMutexUnlock(handle->rx_mutex);
    return A_STATUS_OK;
}

aStatus_t aDevUsartReadAsyncCancel(aDevUsartHandle_t *handle,
                                   aDevUsartReadToken_t token)
{
    if ((handle == NULL) || (token == 0U)) return A_STATUS_INVALID_PARAM;
    if (!handle->drv_handle.initialized) return A_STATUS_NOT_READY;
    aStatus_t status = aOSMutexLock(handle->rx_mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;

    aDevUsartReadNode_t *previous = NULL;
    aDevUsartReadNode_t *node = handle->rx_request_head;
    while ((node != NULL) && (node->token != token)) {
        previous = node;
        node = node->next;
    }
    if (node == NULL) {
        for (node = handle->rx_complete_head; node != NULL; node = node->next) {
            if (node->token == token) break;
        }
        status = node != NULL ? A_STATUS_BUSY : A_STATUS_NOT_READY;
    } else {
        request_remove(handle, previous, node);
        node->event = (aDevUsartRxEvent_t) {
            .type = ADEV_USART_RX_EVENT_CANCELLED,
            .status = A_STATUS_CANCELLED,
        };
        if (handle->rx_complete_tail == NULL)
            handle->rx_complete_head = node;
        else
            handle->rx_complete_tail->next = node;
        handle->rx_complete_tail = node;
        status = A_STATUS_OK;
    }
    if (status == A_STATUS_OK) rx_timer_rearm(handle);
    (void)aOSMutexUnlock(handle->rx_mutex);
    if (status == A_STATUS_OK) {
        (void)aOSWorkSubmit(&handle->rx_completion_work,
                            aDevUsartAsyncRxWork, handle);
    }
    return status;
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
