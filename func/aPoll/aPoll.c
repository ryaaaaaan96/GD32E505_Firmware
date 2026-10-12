#include "aPoll_instance.h"

#include <limits.h>
#include <string.h>

void aPollConfigStructInit(aPollConfig_t *config)
{
    if (config != NULL) {
        const aPollConfig_t defaults = {0};
        *config = defaults;
    }
}

void aPollJobConfigStructInit(aPollJobConfig_t *config)
{
    if (config != NULL) {
        const aPollJobConfig_t defaults = {
            .step_count = 1U, .error_delay_ms = 5U
        };
        *config = defaults;
    }
}

void aPollEventConfigStructInit(aPollEventConfig_t *config)
{
    if (config != NULL) {
        const aPollEventConfig_t defaults = {0};
        *config = defaults;
    }
}

void aPollRequestStructInit(aPollRequest_t *request)
{
    if (request != NULL) {
        const aPollRequest_t defaults = {0};
        *request = defaults;
    }
}

static aStatus_t config_check(const aPollConfig_t *config)
{
    if (config == NULL ||
        (config->job_count != 0U && config->jobs == NULL) ||
        (config->event_count != 0U && config->events == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    for (size_t i = 0U; i < config->job_count; i++) {
        const aPollJobConfig_t *job = &config->jobs[i];
        if (job->execute == NULL || job->step_count == 0U ||
            (job->poll_time_ms > INT32_MAX &&
             job->poll_time_ms != APOLL_POLL_ONCE) ||
            job->interval_ms > INT32_MAX ||
            job->error_delay_ms == 0U || job->error_delay_ms > INT32_MAX) {
            return A_STATUS_INVALID_PARAM;
        }
    }
    for (size_t i = 0U; i < config->event_count; i++) {
        if (config->events[i].function == NULL) return A_STATUS_INVALID_PARAM;
    }
    return A_STATUS_OK;
}

static aStatus_t instance_init(const aPollConfig_t *config,
                              aPollHandle_t *handle, aBool_t dynamic)
{
    aStatus_t status = config_check(config);
    if (status != A_STATUS_OK) return status;
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->ready) return A_STATUS_BUSY;
    if (handle->job_capacity < config->job_count ||
        handle->event_capacity < config->event_count ||
        handle->request_capacity < config->request_capacity ||
        (config->job_count != 0U && handle->jobs == NULL) ||
        (config->event_count != 0U && handle->events == NULL) ||
        (config->request_capacity != 0U && handle->requests == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    handle->wake = NULL;
    handle->queue_mutex = NULL;
    status = aOSWaitObjectCreate(&handle->wake);
    if (status != A_STATUS_OK) return status;
    if (config->request_capacity != 0U) {
        status = aOSMutexCreate(&handle->queue_mutex);
        if (status != A_STATUS_OK) {
            aOSWaitObjectDestroy(&handle->wake);
            return status;
        }
    }
    handle->config = *config;
    handle->head = 0U;
    handle->used = 0U;
    handle->job_cursor = 0U;
    handle->event_cursor = 0U;
    handle->class_cursor = 0U;
    handle->dynamic = dynamic;
    aAtomicInit(&handle->active, A_FALSE);
    for (size_t i = 0U; i < config->job_count; i++) {
        aPollJobState_t *state = &handle->jobs[i];
        state->step_index = 0U;
        state->cycle_started = aOSGetUptimeMs();
        state->delay = aTimepointCalc(A_TIMEOUT_NO_WAIT,
                                      state->cycle_started);
        state->cycle_active = A_FALSE;
        aAtomicInit(&state->triggered, A_TRUE);
    }
    for (size_t i = 0U; i < config->event_count; i++) {
        aAtomicInit(&handle->events[i].pending, A_FALSE);
    }
    handle->ready = A_TRUE;
    return A_STATUS_OK;
}

static aStatus_t instance_deinit(aPollHandle_t *handle)
{
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (aAtomicLoad(&handle->active) || handle->used != 0U) {
        return A_STATUS_BUSY;
    }
    /* 生命周期由应用串行化；必须先停止投递者和等待任务。 */
    handle->ready = A_FALSE;
    aOSMutexDestroy(&handle->queue_mutex);
    aOSWaitObjectDestroy(&handle->wake);
    return A_STATUS_OK;
}

#if APOLL_STATIC_ENABLE
aStatus_t aPollInitStatic(const aPollConfig_t *config, aPollHandle_t *handle)
{
    return instance_init(config, handle, A_FALSE);
}

aStatus_t aPollDeInitStatic(aPollHandle_t *handle)
{
    if (handle == NULL || handle->dynamic) return A_STATUS_INVALID_PARAM;
    return instance_deinit(handle);
}
#endif

#if APOLL_DYNAMIC_ENABLE
/* 各数组按标准最大对齐依次排布，一次申请；每一步均检查乘加溢出。 */
static aBool_t reserve(size_t *size, size_t count, size_t unit, size_t *offset)
{
    const size_t align = _Alignof(max_align_t);
    size_t padding = (align - *size % align) % align;
    if (*size > SIZE_MAX - padding) return A_FALSE;
    *size += padding;
    *offset = *size;
    if (count > (SIZE_MAX - *size) / unit) return A_FALSE;
    *size += count * unit;
    return A_TRUE;
}

aStatus_t aPollCreate(const aPollConfig_t *config, aPollHandle_t **handle_out)
{
    aPollHandle_t *handle;
    size_t size = sizeof(*handle), jobs, events, requests;
    aStatus_t status;
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    status = config_check(config);
    if (status != A_STATUS_OK) return status;
    if (!reserve(&size, config->job_count, sizeof(aPollJobState_t), &jobs) ||
        !reserve(&size, config->event_count, sizeof(aPollEventState_t),
                 &events) ||
        !reserve(&size, config->request_capacity, sizeof(aPollRequest_t),
                 &requests)) return A_STATUS_INVALID_PARAM;
    handle = aOSAlloc(size);
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    memset(handle, 0, sizeof(*handle));
    handle->jobs = (void *)((uint8_t *)handle + jobs);
    handle->events = (void *)((uint8_t *)handle + events);
    handle->requests = (void *)((uint8_t *)handle + requests);
    handle->job_capacity = config->job_count;
    handle->event_capacity = config->event_count;
    handle->request_capacity = config->request_capacity;
    status = instance_init(config, handle, A_TRUE);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aPollDestroy(aPollHandle_t *handle)
{
    aStatus_t status;
    if (handle == NULL) return A_STATUS_OK;
    if (!handle->dynamic) return A_STATUS_INVALID_PARAM;
    status = instance_deinit(handle);
    if (status == A_STATUS_OK) aOSFree(handle);
    return status;
}
#endif

aStatus_t aPollEventPost(aPollHandle_t *handle, size_t event_index)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (event_index >= handle->config.event_count) return A_STATUS_NOT_FOUND;
    aAtomicStore(&handle->events[event_index].pending, A_TRUE);
    aOSWaitObjectNotify(handle->wake);
    return A_STATUS_OK;
}

aStatus_t aPollJobTrigger(aPollHandle_t *handle, size_t job_index)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (job_index >= handle->config.job_count) return A_STATUS_NOT_FOUND;
    aAtomicStore(&handle->jobs[job_index].triggered, A_TRUE);
    aOSWaitObjectNotify(handle->wake);
    return A_STATUS_OK;
}

aStatus_t aPollSubmit(aPollHandle_t *handle, const aPollRequest_t *request)
{
    aStatus_t status;
    size_t tail;
    if (handle == NULL || request == NULL || request->execute == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (handle->config.request_capacity == 0U) return A_STATUS_UNSUPPORTED;
    status = aOSMutexLock(handle->queue_mutex, A_TIMEOUT_NO_WAIT);
    if (status != A_STATUS_OK) return status;
    if (handle->used == handle->config.request_capacity) {
        (void)aOSMutexUnlock(handle->queue_mutex);
        return A_STATUS_BUSY;
    }
    tail = handle->config.request_capacity - handle->head;
    tail = handle->used >= tail ? handle->used - tail :
                                 handle->head + handle->used;
    handle->requests[tail] = *request;
    handle->used++;
    /* 解锁异常由 aOS 记录；已入队请求仍按接收成功返回。 */
    (void)aOSMutexUnlock(handle->queue_mutex);
    aOSWaitObjectNotify(handle->wake);
    return A_STATUS_OK;
}

static uint32_t job_remaining(aPollHandle_t *handle, size_t index,
                              uint32_t now)
{
    const aPollJobConfig_t *job = &handle->config.jobs[index];
    aPollJobState_t *state = &handle->jobs[index];
    if (state->step_index == job->step_count) return UINT32_MAX;
    uint32_t remaining = aTimepointRemaining(&state->delay, now).milliseconds;
    /* 单轮标记不是时间值，不参与周期差值计算。 */
    if (job->poll_time_ms != APOLL_POLL_ONCE &&
        !state->cycle_active && !aAtomicLoad(&state->triggered)) {
        uint32_t elapsed = now - state->cycle_started;
        if (elapsed < job->poll_time_ms &&
            job->poll_time_ms - elapsed > remaining) {
            remaining = job->poll_time_ms - elapsed;
        }
    }
    return remaining;
}

static aBool_t job_process(aPollHandle_t *handle, aPollProcessResult_t *result)
{
    size_t count = handle->config.job_count;
    for (size_t n = 0U; n < count; n++) {
        size_t i = (handle->job_cursor + n) % count;
        aPollJobState_t *state = &handle->jobs[i];
        const aPollJobConfig_t *job = &handle->config.jobs[i];
        uint32_t now = aOSGetUptimeMs();
        uint32_t delay;
        if (job_remaining(handle, i, now) != 0U) continue;
        if (!state->cycle_active) {
            aAtomicExchange(&state->triggered, A_FALSE);
            state->cycle_started = now;
            state->cycle_active = A_TRUE;
        }
        result->type = APOLL_WORK_JOB;
        result->index = i;
        result->step_index = state->step_index;
        job->execute(job->context, state->step_index, &result->execution);
        delay = result->execution.status == A_STATUS_OK &&
                result->execution.advance ? job->interval_ms :
                                            job->error_delay_ms;
        if (result->execution.advance) {
            state->step_index++;
            if (state->step_index == job->step_count) {
                state->cycle_active = A_FALSE;
                if (job->poll_time_ms != APOLL_POLL_ONCE) {
                    state->step_index = 0U;
                }
            }
        }
        state->delay = aTimepointCalc(A_TIMEOUT_MS(delay), aOSGetUptimeMs());
        handle->job_cursor = (i + 1U) % count;
        return A_TRUE;
    }
    return A_FALSE;
}

static aBool_t event_process(aPollHandle_t *handle,
                             aPollProcessResult_t *result)
{
    size_t count = handle->config.event_count;
    for (size_t n = 0U; n < count; n++) {
        size_t i = (handle->event_cursor + n) % count;
        const aPollEventConfig_t *event = &handle->config.events[i];
        if (!aAtomicExchange(&handle->events[i].pending, A_FALSE)) continue;
        result->type = APOLL_WORK_EVENT;
        result->index = i;
        event->function(event->context);
        result->execution.status = A_STATUS_OK;
        result->execution.advance = A_TRUE;
        handle->event_cursor = (i + 1U) % count;
        return A_TRUE;
    }
    return A_FALSE;
}

static aStatus_t request_process(aPollHandle_t *handle,
                                 aPollProcessResult_t *result)
{
    aPollRequest_t request;
    aStatus_t status;
    if (handle->config.request_capacity == 0U) return A_STATUS_OK;
    status = aOSMutexLock(handle->queue_mutex, A_TIMEOUT_NO_WAIT);
    if (status != A_STATUS_OK) return status;
    if (handle->used == 0U) return aOSMutexUnlock(handle->queue_mutex);
    request = handle->requests[handle->head];
    handle->head = (handle->head + 1U) % handle->config.request_capacity;
    handle->used--;
    status = aOSMutexUnlock(handle->queue_mutex);
    /* 取出后必须执行，避免已接受请求在解锁故障时静默丢失。 */
    result->type = APOLL_WORK_REQUEST;
    request.execute(request.context, 0U, &result->execution);
    if (request.complete != NULL) {
        request.complete(request.context, &result->execution);
    }
    return status;
}

static aTimeout_t next_wait(aPollHandle_t *handle)
{
    aTimeout_t wait = A_TIMEOUT_FOREVER;
    uint32_t now = aOSGetUptimeMs();
    for (size_t i = 0U; i < handle->config.job_count; i++) {
        uint32_t remaining = job_remaining(handle, i, now);
        if (remaining == UINT32_MAX) continue;
        if (wait.type == A_TIMEOUT_TYPE_FOREVER ||
            remaining < wait.milliseconds) wait = A_TIMEOUT_MS(remaining);
    }
    for (size_t i = 0U; i < handle->config.event_count; i++) {
        if (aAtomicLoad(&handle->events[i].pending)) return A_TIMEOUT_NO_WAIT;
    }
    if (handle->config.request_capacity != 0U) {
        aBool_t pending;
        if (aOSMutexLock(handle->queue_mutex, A_TIMEOUT_NO_WAIT) !=
            A_STATUS_OK) {
            return wait.type == A_TIMEOUT_TYPE_FOREVER ||
                   wait.milliseconds > 1U ? A_TIMEOUT_MS(1U) : wait;
        }
        pending = handle->used != 0U;
        (void)aOSMutexUnlock(handle->queue_mutex);
        if (pending) return A_TIMEOUT_NO_WAIT;
    }
    return wait;
}

static aStatus_t enter(aPollHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (aAtomicExchange(&handle->active, A_TRUE)) return A_STATUS_BUSY;
    return A_STATUS_OK;
}

aStatus_t aPollProcess(aPollHandle_t *handle, aPollProcessResult_t *result)
{
    aStatus_t status;
    const aPollProcessResult_t defaults = {
        .type = APOLL_WORK_NONE,
        .execution = {.status = A_STATUS_ERROR, .advance = A_FALSE},
        .wait = A_TIMEOUT_NO_WAIT
    };
    if (result == NULL) return A_STATUS_INVALID_PARAM;
    *result = defaults;
    status = enter(handle);
    if (status != A_STATUS_OK) return status;
    for (unsigned n = 0U; n < 3U; n++) {
        unsigned which = (handle->class_cursor + n) % 3U;
        if (which == 0U) (void)job_process(handle, result);
        else if (which == 1U) (void)event_process(handle, result);
        else {
            status = request_process(handle, result);
            /* 队列短暂竞争不推迟其他已到期工作。 */
            if (status == A_STATUS_BUSY && result->type == APOLL_WORK_NONE) {
                status = A_STATUS_OK;
            }
        }
        if (result->type != APOLL_WORK_NONE || status != A_STATUS_OK) {
            handle->class_cursor = (which + 1U) % 3U;
            break;
        }
    }
    result->wait = next_wait(handle);
    aAtomicStore(&handle->active, A_FALSE);
    return status;
}

aStatus_t aPollWait(aPollHandle_t *handle, aTimeout_t timeout)
{
    aStatus_t status;
    aTimeout_t due;
    if (!aTimeoutIsValid(timeout)) return A_STATUS_INVALID_PARAM;
    status = enter(handle);
    if (status != A_STATUS_OK) return status;
    /* 重新计算期限，避免调用者在 Process 之后耗时导致过期等待。 */
    due = next_wait(handle);
    if (timeout.type == A_TIMEOUT_TYPE_FOREVER ||
        (due.type != A_TIMEOUT_TYPE_FOREVER &&
         due.milliseconds < timeout.milliseconds)) timeout = due;
    status = aOSWaitObjectWait(handle->wake, timeout);
    aAtomicStore(&handle->active, A_FALSE);
    return status;
}
