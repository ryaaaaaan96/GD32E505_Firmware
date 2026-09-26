#include "aDev_usart_internal.h"

#include <string.h>

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
    if (!ADEV_USART_HAS_ASYNC || !handle->rx_dma_active ||
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
