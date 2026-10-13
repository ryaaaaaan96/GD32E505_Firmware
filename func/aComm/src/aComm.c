#include "aComm_table_internal.h"

#include <limits.h>
#include <string.h>

void aCommConfigStructInit(aCommConfig_t *config)
{
    if (config != NULL) {
        const aCommConfig_t defaults = {.error_delay_ms = 5U};
        *config = defaults;
    }
}

void aCommJobConfigStructInit(aCommJobConfig_t *config)
{
    if (config != NULL) {
        const aCommJobConfig_t defaults = {
            .step_count = 1U, .error_delay_ms = 5U
        };
        *config = defaults;
    }
}

void aCommEventConfigStructInit(aCommEventConfig_t *config)
{
    if (config != NULL) {
        const aCommEventConfig_t defaults = {0};
        *config = defaults;
    }
}

void aCommRequestStructInit(aCommRequest_t *request)
{
    if (request != NULL) {
        const aCommRequest_t defaults = {0};
        *request = defaults;
    }
}

static aBool_t table_enabled(const aCommConfig_t *config)
{
    return config->table.items != NULL || config->table.count != 0U;
}
static const aCommJobConfig_t *job_get(aCommHandle_t *handle, size_t index)
{
    return table_enabled(&handle->config) ?
        &handle->table_jobs[index].config : &handle->config.jobs[index];
}
static void table_execute(void *context, size_t step,
    aCommStepResult_t *result)
{
    aCommTableJob_t *job = context;
    const aCommPlanRequest_t request = {
        .frame_index = job->first_frame + step, .execution = result
    };
    result->status = aCommPlanExecute(job->plan, &request);
}
static void table_jobs_init(aCommHandle_t *handle)
{
    size_t count = aCommPlanGroupCount(&handle->plan);
    handle->config.job_count = count;
    for (size_t i = 0U; i < count; i++) {
        aCommPlanGroup_t group;
        (void)aCommPlanGroupGet(&handle->plan, i, &group);
        aCommTableJob_t *job = &handle->table_jobs[i];
        *job = (aCommTableJob_t){
            .plan = &handle->plan, .first_frame = group.first_frame,
            .config = {
                .execute = table_execute, .context = job,
                .step_count = group.frame_count,
                .poll_time_ms = group.poll_time_ms,
                .retry_count = handle->config.retry_count,
                .interval_ms = handle->config.interval_ms,
                .error_delay_ms = handle->config.error_delay_ms
            }
        };
    }
}

static aStatus_t config_check(const aCommConfig_t *config)
{
    if (config == NULL ||
        (config->job_count != 0U && config->jobs == NULL) ||
        (config->event_count != 0U && config->events == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (table_enabled(config)) {
        if (config->job_count != 0U || config->jobs != NULL ||
            config->interval_ms > INT32_MAX ||
            config->error_delay_ms == 0U ||
            config->error_delay_ms > INT32_MAX) return A_STATUS_INVALID_PARAM;
    }
    for (size_t i = 0U; i < config->job_count; i++) {
        const aCommJobConfig_t *job = &config->jobs[i];
        if (job->execute == NULL || job->step_count == 0U ||
            (job->poll_time_ms > INT32_MAX &&
             job->poll_time_ms != ACOMM_POLL_ONCE) ||
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

static aStatus_t registration_check(const aCommConfig_t *config)
{
    aStatus_t status = config_check(config);
    if (status != A_STATUS_OK) return status;
    const aCommProtocol_t *protocol = &config->protocol;
    if (protocol->ops == NULL) {
        return protocol->config != NULL || protocol->items != NULL ||
            protocol->count != 0U || protocol->context != NULL ?
            A_STATUS_INVALID_PARAM : A_STATUS_OK;
    }
    if (table_enabled(config) || config->jobs != NULL ||
        config->job_count != 0U || protocol->ops->init == NULL ||
        protocol->ops->deinit == NULL ||
        (protocol->count != 0U && protocol->items == NULL) ||
        config->interval_ms > INT32_MAX || config->error_delay_ms == 0U ||
        config->error_delay_ms > INT32_MAX) return A_STATUS_INVALID_PARAM;
    return A_STATUS_OK;
}
aStatus_t aCommConfigCheck(const aCommConfig_t *config)
{
    aStatus_t status = registration_check(config);
    if (status != A_STATUS_OK) return status;
    const aCommProtocol_t *protocol = &config->protocol;
    return protocol->ops == NULL || protocol->ops->check == NULL ? A_STATUS_OK :
        protocol->ops->check(protocol);
}

/* 初始化回调只准备协议和计划；调度资源分配失败时统一回收协议。 */
static aStatus_t protocol_prepare(const aCommConfig_t *config,
    aCommConfig_t *resolved, aCommJobConfig_t *job)
{
    aStatus_t status = registration_check(config);
    if (status != A_STATUS_OK) return status;
    *resolved = *config;
    const aCommProtocol_t *protocol = &config->protocol;
    if (protocol->ops == NULL) return A_STATUS_OK;
    status = protocol->ops->init(protocol, &resolved->table);
    if (status != A_STATUS_OK) return status;
    if (!table_enabled(resolved)) {
        aCommJobConfigStructInit(job);
        job->execute = protocol->ops->process;
        job->context = protocol->context;
        job->retry_count = config->retry_count;
        job->interval_ms = config->interval_ms;
        job->error_delay_ms = config->error_delay_ms;
        resolved->jobs = job;
        resolved->job_count = 1U;
    }
    return A_STATUS_OK;
}
static aStatus_t protocol_cleanup(const aCommConfig_t *config,
    aStatus_t status)
{
    if (config->protocol.ops != NULL) {
        aStatus_t cleanup = config->protocol.ops->deinit(
            config->protocol.context);
        if (cleanup != A_STATUS_OK) return cleanup;
    }
    return status;
}

static aStatus_t instance_init(const aCommConfig_t *config,
    aCommHandle_t *handle, aBool_t dynamic, const aCommPlanSize_t *layout)
{
    aStatus_t status;
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->ready) return A_STATUS_BUSY;
    size_t job_count = config->job_count;
    if (table_enabled(config)) {
        job_count = layout->groups;
        if (handle->table_capacity < job_count ||
            (job_count != 0U && handle->table_jobs == NULL)) {
            return A_STATUS_INVALID_PARAM;
        }
    }
    if (handle->job_capacity < job_count ||
        handle->event_capacity < config->event_count ||
        handle->request_capacity < config->request_capacity ||
        (job_count != 0U && handle->jobs == NULL) ||
        (config->event_count != 0U && handle->events == NULL) ||
        (config->request_capacity != 0U && handle->requests == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    handle->wake = NULL;
    handle->queue_mutex = NULL;
    status = aOSWaitObjectCreate(&handle->wake);
    if (status != A_STATUS_OK) goto cleanup;
    if (config->request_capacity != 0U) {
        status = aOSMutexCreate(&handle->queue_mutex);
        if (status != A_STATUS_OK) goto cleanup;
    }
    if (table_enabled(config)) {
        status = aCommPlanBindMeasured(&config->table, &handle->plan, layout);
        if (status != A_STATUS_OK) goto cleanup;
    }
    handle->config = *config;
    if (config->protocol.ops != NULL && !table_enabled(config)) {
        handle->protocol_job = *config->jobs;
        handle->config.jobs = &handle->protocol_job;
    }
    handle->stopping = A_FALSE;
    if (table_enabled(config)) table_jobs_init(handle);
    handle->head = 0U;
    handle->used = 0U;
    handle->job_cursor = 0U;
    handle->event_cursor = 0U;
    handle->class_cursor = 0U;
    handle->dynamic = dynamic;
    aAtomicInit(&handle->active, A_FALSE);
    for (size_t i = 0U; i < job_count; i++) {
        aCommJobState_t *state = &handle->jobs[i];
        state->step_index = 0U;
        state->retries = 0U;
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
cleanup:
    aOSMutexDestroy(&handle->queue_mutex);
    aOSWaitObjectDestroy(&handle->wake);
    return status;
}

static aStatus_t instance_deinit(aCommHandle_t *handle)
{
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (aAtomicLoad(&handle->active) || handle->used != 0U) {
        return A_STATUS_BUSY;
    }
    if (handle->plan.ready) {
        aStatus_t status = aCommPlanUnbind(&handle->plan);
        if (status != A_STATUS_OK) return status;
    }
    if (handle->config.protocol.ops != NULL) {
        handle->stopping = A_TRUE;
        aStatus_t status = protocol_cleanup(&handle->config, A_STATUS_OK);
        if (status != A_STATUS_OK) return status;
    }
    /* 生命周期由应用串行化；必须先停止投递者和等待任务。 */
    handle->ready = A_FALSE;
    aOSMutexDestroy(&handle->queue_mutex);
    aOSWaitObjectDestroy(&handle->wake);
    return A_STATUS_OK;
}

#if ACOMM_STATIC_ENABLE
aStatus_t aCommInitStatic(const aCommConfig_t *config, aCommHandle_t *handle)
{
    aCommConfig_t resolved;
    aCommJobConfig_t job;
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->ready) return A_STATUS_BUSY;
    aStatus_t status = protocol_prepare(config, &resolved, &job);
    if (status != A_STATUS_OK) return status;
    aCommPlanSize_t layout = {0};
    status = config_check(&resolved);
    if (status == A_STATUS_OK && table_enabled(&resolved)) {
        status = aCommPlanMeasure(&resolved.table, &layout);
    }
    if (status == A_STATUS_OK) {
        status = instance_init(&resolved, handle, A_FALSE, &layout);
    }
    return status == A_STATUS_OK ? status : protocol_cleanup(&resolved, status);
}

aStatus_t aCommDeInitStatic(aCommHandle_t *handle)
{
    if (handle == NULL || handle->dynamic) return A_STATUS_INVALID_PARAM;
    return instance_deinit(handle);
}
#endif

#if ACOMM_DYNAMIC_ENABLE
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

aStatus_t aCommCreate(const aCommConfig_t *config, aCommHandle_t **handle_out)
{
    aCommHandle_t *handle;
    size_t size = sizeof(*handle), jobs, events, requests;
    aCommPlanSize_t layout = {0};
    size_t frame_offset, index_offset, group_offset;
    aStatus_t status;
    aCommConfig_t resolved;
    aCommJobConfig_t job;
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    status = protocol_prepare(config, &resolved, &job);
    if (status != A_STATUS_OK) return status;
    config = &resolved;
    status = config_check(config);
    if (status != A_STATUS_OK) goto cleanup;
    size_t job_count = config->job_count;
    if (table_enabled(config)) {
        status = aCommPlanMeasure(&config->table, &layout);
        if (status != A_STATUS_OK) goto cleanup;
        job_count = layout.groups;
    }
    if (!reserve(&size, layout.groups, sizeof(aCommTableJob_t),
                 &group_offset) ||
        !reserve(&size, layout.frames, sizeof(aCommPlanFrame_t),
                 &frame_offset) ||
        !reserve(&size, layout.items, sizeof(size_t), &index_offset) ||
        !reserve(&size, job_count, sizeof(aCommJobState_t), &jobs) ||
        !reserve(&size, config->event_count, sizeof(aCommEventState_t),
                 &events) ||
        !reserve(&size, config->request_capacity, sizeof(aCommRequest_t),
                 &requests)) {
        status = A_STATUS_INVALID_PARAM;
        goto cleanup;
    }
    handle = aOSAlloc(size);
    if (handle == NULL) {
        status = A_STATUS_NO_MEMORY;
        goto cleanup;
    }
    memset(handle, 0, sizeof(*handle));
    handle->jobs = (void *)((uint8_t *)handle + jobs);
    handle->events = (void *)((uint8_t *)handle + events);
    handle->requests = (void *)((uint8_t *)handle + requests);
    handle->job_capacity = job_count;
    handle->table_jobs = (void *)((uint8_t *)handle + group_offset);
    handle->table_capacity = layout.groups;
    handle->plan.frames = (void *)((uint8_t *)handle + frame_offset);
    handle->plan.capacity = layout.frames;
    handle->plan.indices = (void *)((uint8_t *)handle + index_offset);
    handle->plan.index_capacity = layout.items;
    handle->event_capacity = config->event_count;
    handle->request_capacity = config->request_capacity;
    status = instance_init(config, handle, A_TRUE, &layout);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        goto cleanup;
    }
    *handle_out = handle;
    return A_STATUS_OK;
cleanup:
    return protocol_cleanup(config, status);
}

aStatus_t aCommDestroy(aCommHandle_t *handle)
{
    aStatus_t status;
    if (handle == NULL) return A_STATUS_OK;
    if (!handle->dynamic) return A_STATUS_INVALID_PARAM;
    status = instance_deinit(handle);
    if (status == A_STATUS_OK) aOSFree(handle);
    return status;
}
#endif

aStatus_t aCommEventPost(aCommHandle_t *handle, size_t event_index)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->ready || handle->stopping) return A_STATUS_NOT_READY;
    if (event_index >= handle->config.event_count) return A_STATUS_NOT_FOUND;
    aAtomicStore(&handle->events[event_index].pending, A_TRUE);
    aOSWaitObjectNotify(handle->wake);
    return A_STATUS_OK;
}

aStatus_t aCommJobTrigger(aCommHandle_t *handle, size_t job_index)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->ready || handle->stopping) return A_STATUS_NOT_READY;
    if (job_index >= handle->config.job_count) return A_STATUS_NOT_FOUND;
    aAtomicStore(&handle->jobs[job_index].triggered, A_TRUE);
    aOSWaitObjectNotify(handle->wake);
    return A_STATUS_OK;
}

aStatus_t aCommSubmit(aCommHandle_t *handle, const aCommRequest_t *request)
{
    aStatus_t status;
    size_t tail;
    if (handle == NULL || request == NULL || request->execute == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->ready || handle->stopping) return A_STATUS_NOT_READY;
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

static uint32_t job_remaining(aCommHandle_t *handle, size_t index,
                              uint32_t now)
{
    const aCommJobConfig_t *job = job_get(handle, index);
    aCommJobState_t *state = &handle->jobs[index];
    if (state->step_index == job->step_count) return UINT32_MAX;
    uint32_t remaining = aTimepointRemaining(&state->delay, now).milliseconds;
    /* 单轮标记不是时间值，不参与周期差值计算。 */
    if (job->poll_time_ms != ACOMM_POLL_ONCE &&
        !state->cycle_active && !aAtomicLoad(&state->triggered)) {
        uint32_t elapsed = now - state->cycle_started;
        if (elapsed < job->poll_time_ms &&
            job->poll_time_ms - elapsed > remaining) {
            remaining = job->poll_time_ms - elapsed;
        }
    }
    return remaining;
}

static aBool_t job_process(aCommHandle_t *handle, aCommProcessResult_t *result)
{
    size_t count = handle->config.job_count;
    for (size_t n = 0U; n < count; n++) {
        size_t i = (handle->job_cursor + n) % count;
        aCommJobState_t *state = &handle->jobs[i];
        const aCommJobConfig_t *job = job_get(handle, i);
        uint32_t now = aOSGetUptimeMs();
        uint32_t delay;
        if (job_remaining(handle, i, now) != 0U) continue;
        if (!state->cycle_active) {
            aAtomicExchange(&state->triggered, A_FALSE);
            state->cycle_started = now;
            state->cycle_active = A_TRUE;
        }
        result->type = ACOMM_WORK_JOB;
        result->index = i;
        result->step_index = state->step_index;
        job->execute(job->context, state->step_index, &result->execution);
        if (result->execution.advance) {
            if (result->execution.status != A_STATUS_OK &&
                result->execution.retryable && state->retries <
                job->retry_count) {
                state->retries++;
                result->execution.advance = A_FALSE;
            } else {
                state->retries = 0U;
            }
        }
        delay = result->execution.status == A_STATUS_OK &&
                result->execution.advance ? job->interval_ms :
                                            job->error_delay_ms;
        if (result->execution.advance) {
            state->step_index++;
            if (state->step_index == job->step_count) {
                state->cycle_active = A_FALSE;
                if (job->poll_time_ms != ACOMM_POLL_ONCE) {
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

static aBool_t event_process(aCommHandle_t *handle,
                             aCommProcessResult_t *result)
{
    size_t count = handle->config.event_count;
    for (size_t n = 0U; n < count; n++) {
        size_t i = (handle->event_cursor + n) % count;
        const aCommEventConfig_t *event = &handle->config.events[i];
        if (!aAtomicExchange(&handle->events[i].pending, A_FALSE)) continue;
        result->type = ACOMM_WORK_EVENT;
        result->index = i;
        event->function(event->context);
        result->execution.status = A_STATUS_OK;
        result->execution.advance = A_TRUE;
        handle->event_cursor = (i + 1U) % count;
        return A_TRUE;
    }
    return A_FALSE;
}

static aStatus_t request_process(aCommHandle_t *handle,
                                 aCommProcessResult_t *result)
{
    aCommRequest_t request;
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
    result->type = ACOMM_WORK_REQUEST;
    result->execution.status = request.execute(request.context);
    result->execution.advance = A_TRUE;
    if (request.complete != NULL) {
        request.complete(request.context, result->execution.status);
    }
    return status;
}

static aTimeout_t next_wait(aCommHandle_t *handle)
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

static aStatus_t enter(aCommHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->ready || handle->stopping) return A_STATUS_NOT_READY;
    if (aAtomicExchange(&handle->active, A_TRUE)) return A_STATUS_BUSY;
    return A_STATUS_OK;
}

aStatus_t aCommProcess(aCommHandle_t *handle, aCommProcessResult_t *result)
{
    aStatus_t status;
    const aCommProcessResult_t defaults = {
        .type = ACOMM_WORK_NONE,
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
            if (status == A_STATUS_BUSY && result->type == ACOMM_WORK_NONE) {
                status = A_STATUS_OK;
            }
        }
        if (result->type != ACOMM_WORK_NONE || status != A_STATUS_OK) {
            handle->class_cursor = (which + 1U) % 3U;
            break;
        }
    }
    result->wait = next_wait(handle);
    aAtomicStore(&handle->active, A_FALSE);
    return status;
}

aStatus_t aCommWait(aCommHandle_t *handle, aTimeout_t timeout)
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

aStatus_t aCommTrigger(aCommHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->ready || handle->stopping) return A_STATUS_NOT_READY;
    for (size_t i = 0U; i < handle->config.job_count; i++) {
        aAtomicStore(&handle->jobs[i].triggered, A_TRUE);
    }
    aOSWaitObjectNotify(handle->wake);
    return A_STATUS_OK;
}
