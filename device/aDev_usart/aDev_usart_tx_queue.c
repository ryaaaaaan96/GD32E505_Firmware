#include "aDev_usart_tx_queue.h"
#include "aDev_usart_internal.h"

#include <string.h>

static void queue_start_next(aDevUsartTxQueueHandle_t *queue);
static void queue_work(void *argument);
static aStatus_t cancel_in_worker(aDevUsartTxQueueHandle_t *queue);
static void queue_tx_complete(aDevUsartHandle_t *usart,
                              const aDevUsartTxEvent_t *event,
                              void *argument);

void aDevUsartTxQueueConfigStructInit(aDevUsartTxQueueConfig_t *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
}

static void queue_pop_locked(aDevUsartTxQueueHandle_t *queue,
                             aDevUsartTxRequest_t *request)
{
    queue->callback_active = A_TRUE;
    (void)aFifoPop(&queue->fifo, request);
}

static aTimeout_t request_remaining(const aDevUsartTxRequest_t *request,
                                    aBool_t *expired)
{
    uint32_t elapsed;

    *expired = A_FALSE;
    if (request->timeout.type == A_TIMEOUT_TYPE_FOREVER) {
        return A_TIMEOUT_FOREVER;
    }
    elapsed = aOSGetUptimeMs() - request->submitted_at_ms;
    if (elapsed >= request->timeout.milliseconds) {
        *expired = A_TRUE;
        return A_TIMEOUT_NO_WAIT;
    }
    return A_TIMEOUT_MS(request->timeout.milliseconds - elapsed);
}

static void report_failed_request(aDevUsartTxQueueHandle_t *queue,
                                  const aDevUsartTxRequest_t *request,
                                  aStatus_t status)
{
    const aDevUsartTxEvent_t event = {
        .buffer = request->buffer,
        .requested = request->length,
        .transferred = 0U,
        .status = status,
    };

    if (queue->callback != NULL) {
        queue->callback(queue, request->id, &event, queue->argument);
    }
    (void)aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER);
    queue->callback_active = A_FALSE;
    aOSWaitObjectNotify(queue->drained);
    (void)aOSMutexUnlock(queue->mutex);
}

static void queue_start_next(aDevUsartTxQueueHandle_t *queue)
{
    for (;;) {
        aDevUsartTxRequest_t request;
        aTimeout_t remaining;
        aBool_t expired;
        aStatus_t status;

        if (aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER) != A_STATUS_OK) {
            return;
        }
        if (!queue->initialized || queue->closing || queue->cancelling || queue->active ||
            (queue->fifo.count == 0U)) {
            (void)aOSMutexUnlock(queue->mutex);
            return;
        }
        (void)aFifoPeek(&queue->fifo, &request);
        remaining = request_remaining(&request, &expired);
        if (expired) {
            queue_pop_locked(queue, &request);
            if (queue->fifo.count == 0U) {
                aOSWaitObjectNotify(queue->drained);
            }
            (void)aOSMutexUnlock(queue->mutex);
            report_failed_request(queue, &request, A_STATUS_TIMEOUT);
            continue;
        }

        /* Keep the queue lock across submission so CancelAll sees a stable active head. */
        queue->active = A_TRUE;
        const aDevUsartWriteRequest_t transfer = {
            .buffer = request.buffer,
            .size = request.length,
            .timeout = remaining,
            .callback = queue_tx_complete,
            .argument = queue,
        };
        status = aDevUsartWriteAsyncQueued(queue->usart, queue, &transfer);
        if (status == A_STATUS_OK) {
            (void)aOSMutexUnlock(queue->mutex);
            return;
        }
        queue->active = A_FALSE;
        queue_pop_locked(queue, &request);
        if (queue->fifo.count == 0U) {
            aOSWaitObjectNotify(queue->drained);
        }
        (void)aOSMutexUnlock(queue->mutex);
        report_failed_request(queue, &request, status);
    }
}

static void queue_tx_complete(aDevUsartHandle_t *usart,
                              const aDevUsartTxEvent_t *event,
                              void *argument)
{
    aDevUsartTxQueueHandle_t *queue = argument;
    (void)usart;
    if (aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER) != A_STATUS_OK) return;
    queue->completion = *event;
    queue->completion_ready = A_TRUE;
    (void)aOSWorkSubmit(queue->work, queue_work, queue);
    (void)aOSMutexUnlock(queue->mutex);
}

static void queue_finish(aDevUsartHandle_t *usart,
                          const aDevUsartTxEvent_t *event, void *argument)
{
    aDevUsartTxQueueHandle_t *queue = argument;
    aDevUsartTxRequest_t request;
    aBool_t start_next;
    aBool_t drain_cancelled;

    (void)usart;
    if (aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER) != A_STATUS_OK) {
        return;
    }
    if (queue->fifo.count == 0U) {
        queue->active = A_FALSE;
        (void)aOSMutexUnlock(queue->mutex);
        return;
    }
    queue_pop_locked(queue, &request);
    queue->active = A_FALSE;
    start_next = (queue->fifo.count != 0U && !queue->cancelling)
                     ? A_TRUE : A_FALSE;
    drain_cancelled = queue->cancelling ? A_TRUE : A_FALSE;
    if (drain_cancelled && (queue->fifo.count == 0U)) {
        queue->cancelling = A_FALSE;
        drain_cancelled = A_FALSE;
    }
    if (!start_next && (queue->fifo.count == 0U)) {
        aOSWaitObjectNotify(queue->drained);
    }
    (void)aOSMutexUnlock(queue->mutex);

    if (queue->callback != NULL) {
        queue->callback(queue, request.id, event, queue->argument);
    }
    (void)aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER);
    queue->callback_active = A_FALSE;
    aOSWaitObjectNotify(queue->drained);
    (void)aOSMutexUnlock(queue->mutex);
    if (start_next) {
        queue_start_next(queue);
        return;
    }
    if (!drain_cancelled) return;

    for (;;) {
        aDevUsartTxRequest_t cancelled;
        aBool_t have_cancelled = A_FALSE;

        if (aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER) != A_STATUS_OK) {
            return;
        }
        if (queue->cancelling && (queue->fifo.count != 0U)) {
            queue_pop_locked(queue, &cancelled);
            have_cancelled = A_TRUE;
        } else {
            queue->cancelling = A_FALSE;
            if (queue->fifo.count == 0U) aOSWaitObjectNotify(queue->drained);
        }
        (void)aOSMutexUnlock(queue->mutex);
        if (!have_cancelled) break;
        report_failed_request(queue, &cancelled, A_STATUS_CANCELLED);
    }
}

aStatus_t aDevUsartTxQueueInit(const aDevUsartTxQueueConfig_t *config,
                            aDevUsartTxQueueHandle_t *queue)
{
    aStatus_t status;

    if ((config == NULL) || (queue == NULL) || (config->usart == NULL) ||
        (config->request_storage == NULL) ||
        (config->request_capacity == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!aDevUsartIsSupported(config->usart, ADEV_USART_CAP_TX_DIRECT)) {
        return A_STATUS_UNSUPPORTED;
    }
    memset(queue, 0, sizeof(*queue));
    queue->usart = config->usart;
    status = aFifoInit(&queue->fifo, config->request_storage,
                       config->request_capacity, sizeof(aDevUsartTxRequest_t));
    if (status != A_STATUS_OK) return status;
    queue->work = aOSAlloc(sizeof(aOSWorkItem_t));
    if (queue->work == NULL) return A_STATUS_NO_MEMORY;
    aOSWorkItemInit(queue->work);
    queue->next_id = 1U;
    queue->callback = config->callback;
    queue->argument = config->argument;

    status = aOSMutexCreate(&queue->mutex);
    if (status == A_STATUS_OK) {
        status = aOSWaitObjectCreate(&queue->drained);
    }
    if (status == A_STATUS_OK) {
        status = aDevUsartTxQueueClaim(queue->usart, queue);
    }
    if (status != A_STATUS_OK) {
        aOSWaitObjectDestroy(&queue->drained);
        aOSMutexDestroy(&queue->mutex);
        aOSFree(queue->work);
        memset(queue, 0, sizeof(*queue));
        return status;
    }
    queue->initialized = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aDevUsartTxQueueSubmit(aDevUsartTxQueueHandle_t *queue,
                              const aDevUsartTxQueueRequest_t *submission,
                              uint32_t *request_id)
{
    if (submission == NULL) return A_STATUS_INVALID_PARAM;
    const void *buffer = submission->buffer;
    const size_t length = submission->size;
    const aTimeout_t timeout = submission->timeout;
    aDevUsartTxRequest_t request;
    aStatus_t status;

    if ((queue == NULL) || !queue->initialized || (buffer == NULL) ||
        (length == 0U) || !aTimeoutIsValid(timeout) ||
        ((timeout.type == A_TIMEOUT_TYPE_RELATIVE) &&
         (timeout.milliseconds == 0U))) {
        return A_STATUS_INVALID_PARAM;
    }
    status = aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    if (queue->cancelling || queue->closing) {
        (void)aOSMutexUnlock(queue->mutex);
        return A_STATUS_BUSY;
    }
    if (queue->fifo.count == queue->fifo.capacity) {
        (void)aOSMutexUnlock(queue->mutex);
        return A_STATUS_BUSY;
    }
    status = aOSWorkSubmit(queue->work, queue_work, queue);
    if (status != A_STATUS_OK && status != A_STATUS_BUSY) {
        (void)aOSMutexUnlock(queue->mutex);
        return status;
    }
    request = (aDevUsartTxRequest_t) {
        .buffer = buffer, .length = length, .timeout = timeout,
        .submitted_at_ms = aOSGetUptimeMs(), .id = queue->next_id++,
    };
    if (queue->next_id == 0U) queue->next_id = 1U;
    (void)aFifoPush(&queue->fifo, &request);
    if (request_id != NULL) *request_id = request.id;
    (void)aOSMutexUnlock(queue->mutex);
    return A_STATUS_OK;
}

static aStatus_t cancel_in_worker(aDevUsartTxQueueHandle_t *queue)
{
    aBool_t active;
    aStatus_t status;

    if ((queue == NULL) || !queue->initialized) return A_STATUS_INVALID_PARAM;
    status = aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    active = queue->active;
    if (queue->fifo.count == 0U) {
        (void)aOSMutexUnlock(queue->mutex);
        return A_STATUS_OK;
    }
    if (!active) {
        queue->cancelling = A_TRUE;
        (void)aOSMutexUnlock(queue->mutex);
        for (;;) {
            aDevUsartTxRequest_t cancelled;
            status = aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER);
            if (status != A_STATUS_OK) return status;
            if (queue->fifo.count == 0U) {
                queue->cancelling = A_FALSE;
                aOSWaitObjectNotify(queue->drained);
                (void)aOSMutexUnlock(queue->mutex);
                return A_STATUS_OK;
            }
            queue_pop_locked(queue, &cancelled);
            (void)aOSMutexUnlock(queue->mutex);
            report_failed_request(queue, &cancelled, A_STATUS_CANCELLED);
        }
    }
    queue->cancelling = A_TRUE;
    (void)aOSMutexUnlock(queue->mutex);

    status = aDevUsartWriteAsyncCancelQueued(queue->usart, queue);
    return status == A_STATUS_NOT_READY ? A_STATUS_OK : status;
}

static void queue_work(void *argument)
{
    aDevUsartTxQueueHandle_t *queue = argument;
    if (aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER) != A_STATUS_OK) return;
    const aBool_t closing = queue->closing;
    const aBool_t ready = queue->completion_ready;
    const aBool_t cancelling = queue->cancelling;
    const aDevUsartTxEvent_t event = queue->completion;
    queue->completion_ready = A_FALSE;
    (void)aOSMutexUnlock(queue->mutex);
    if (closing) return;
    if (ready) queue_finish(queue->usart, &event, queue);
    else if (cancelling) (void)cancel_in_worker(queue);
    else queue_start_next(queue);
}

aStatus_t aDevUsartTxQueueCancelAll(aDevUsartTxQueueHandle_t *queue)
{
    if (queue == NULL || !queue->initialized) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    status = aOSWorkSubmit(queue->work, queue_work, queue);
    if (status == A_STATUS_OK || status == A_STATUS_BUSY) {
        queue->cancelling = A_TRUE;
        status = A_STATUS_OK;
    }
    (void)aOSMutexUnlock(queue->mutex);
    return status;
}

aStatus_t aDevUsartTxQueueWaitDrained(aDevUsartTxQueueHandle_t *queue,
                                   aTimeout_t timeout)
{
    aTimepoint_t deadline;
    aStatus_t status;

    if ((queue == NULL) || !queue->initialized || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    for (;;) {
        status = aOSMutexLock(queue->mutex,
                              aTimepointRemaining(&deadline,
                                                  aOSGetUptimeMs()));
        if (status != A_STATUS_OK) return status;
        if (queue->fifo.count == 0U && !queue->callback_active) {
            (void)aOSMutexUnlock(queue->mutex);
            return aDevUsartWaitTransmitComplete(
                queue->usart,
                aTimepointRemaining(&deadline, aOSGetUptimeMs()));
        }
        (void)aOSMutexUnlock(queue->mutex);
        status = aOSWaitObjectWait(
            queue->drained,
            aTimepointRemaining(&deadline, aOSGetUptimeMs()));
        if (status != A_STATUS_OK) return status;
    }
}

size_t aDevUsartTxQueueGetPendingCount(aDevUsartTxQueueHandle_t *queue)
{
    size_t count = 0U;
    if ((queue == NULL) || !queue->initialized) return 0U;
    if (aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER) == A_STATUS_OK) {
        count = queue->fifo.count + (queue->callback_active ? 1U : 0U);
        (void)aOSMutexUnlock(queue->mutex);
    }
    return count;
}

aBool_t aDevUsartTxQueueIsIdle(aDevUsartTxQueueHandle_t *queue)
{
    return aDevUsartTxQueueGetPendingCount(queue) == 0U ? A_TRUE : A_FALSE;
}

aStatus_t aDevUsartTxQueueDeInit(aDevUsartTxQueueHandle_t *queue)
{
    aStatus_t status;

    if ((queue == NULL) || !queue->initialized) return A_STATUS_INVALID_PARAM;
    status = aOSMutexLock(queue->mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    if (queue->fifo.count != 0U || queue->callback_active || queue->active) {
        (void)aOSMutexUnlock(queue->mutex);
        return A_STATUS_BUSY;
    }
    queue->closing = A_TRUE;
    (void)aOSMutexUnlock(queue->mutex);
    status = aOSWorkWaitIdle(queue->work, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) { queue->closing = A_FALSE; return status; }
    status = aDevUsartTxQueueRelease(queue->usart, queue);
    if (status != A_STATUS_OK) { queue->closing = A_FALSE; return status; }
    queue->initialized = A_FALSE;
    aOSFree(queue->work);
    aOSWaitObjectDestroy(&queue->drained);
    aOSMutexDestroy(&queue->mutex);
    memset(queue, 0, sizeof(*queue));
    return A_STATUS_OK;
}
