/* 不包含或链接任何协议栈：验证具体数组步长、分组、重试及生命周期。 */
#include "aComm_instance.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void testSetTime(uint32_t ms);
void testAdvanceTime(uint32_t ms);
unsigned testAllocations(void);
void testFailWaitCreate(aBool_t fail);
void aOSDelayMs(uint32_t ms) { testAdvanceTime(ms); }

typedef struct {
    aCommEntry_t comm;
    unsigned token;
    char payload[37];
} entry_t;
static const entry_t entries[] = {
    {.comm = {.poll_time_ms = 10U}, .token = 101U},
    {.comm = {.poll_time_ms = 0U}, .token = 555U},
    {.comm = {.poll_time_ms = 10U}, .token = 102U},
    {.comm = {.poll_time_ms = ACOMM_POLL_ONCE}, .token = 901U},
    {.comm = {.poll_time_ms = 10U, .flags = ACOMM_MAP_FLAG_BREAK_BEFORE},
     .token = 103U}
};
typedef struct {
    size_t references, prepared, calls;
    unsigned seen[16];
    aBool_t fail_prepare, fail_once, busy_release;
} context_t;
static const aCommEntry_t *get(const void *table, size_t index)
{
    const entry_t *items = table;
    assert(index < 5U);
    return &items[index].comm;
}
static aStatus_t check(void *context, const aCommEntry_t *entry)
{
    (void)context;
    const entry_t *item = (const void *)entry;
    return item->token >= 101U ? A_STATUS_OK : A_STATUS_INVALID_PARAM;
}
static aBool_t merge(void *context, const aCommEntry_t *first,
    const aCommEntry_t *previous, const aCommEntry_t *next)
{
    (void)context; (void)first;
    return ((const entry_t *)next)->token ==
           ((const entry_t *)previous)->token + 1U;
}
static aStatus_t acquire(void *context)
{
    ((context_t *)context)->references++;
    return A_STATUS_OK;
}
static aStatus_t release(void *context)
{
    context_t *state = context;
    if (state->busy_release) return A_STATUS_BUSY;
    assert(state->references == 1U);
    state->references--;
    return A_STATUS_OK;
}
static aStatus_t prepare(void *context, const aCommBatch_t *batch,
    aCommPrepared_t *prepared)
{
    context_t *state = context;
    if (state->fail_prepare) return A_STATUS_ERROR;
    unsigned token = ((const entry_t *)batch->entries)[batch->indices[0]].token;
    memcpy(prepared->data, &token, sizeof(token));
    prepared->size = sizeof(token);
    state->prepared++;
    return A_STATUS_OK;
}
static void execute(void *context, const aCommBatch_t *batch,
    const aCommPrepared_t *prepared, aCommStepResult_t *execution,
    aCommEndpointResult_t *result)
{
    context_t *state = context;
    const entry_t *items = batch->entries;
    unsigned token = items[batch->indices[0]].token;
    unsigned saved;
    if (prepared->size != 0U) {
        assert(prepared->size == sizeof(saved));
        memcpy(&saved, prepared->data, sizeof(saved));
        assert(token == saved);
    }
    if (token == 101U) {
        assert(batch->count == 2U && batch->indices[1] == 2U);
    } else assert(batch->count == 1U);
    assert(state->calls < 16U);
    state->seen[state->calls++] = token;
    *result = (aCommEndpointResult_t){.started = A_TRUE};
    *execution = (aCommStepResult_t){.status = A_STATUS_OK, .advance = A_TRUE};
    if (token == 901U && state->fail_once) {
        state->fail_once = A_FALSE;
        execution->status = A_STATUS_TIMEOUT;
        execution->retryable = A_TRUE;
    }
}
static const aCommTableOps_t ops = {
    .get = get, .check = check, .merge = merge,
    .prepare = prepare, .execute = execute,
    .acquire = acquire, .release = release
};
#if ACOMM_STATIC_ENABLE
static aCommPlanFrame_t frames[5];
static size_t indices[5];
static aCommJobState_t jobs[5];
static aCommTableJob_t table_jobs[5];
static aCommHandle_t storage = {
    .plan = {.frames = frames, .capacity = 5U,
             .indices = indices, .index_capacity = 5U},
    .table_jobs = table_jobs, .table_capacity = 5U,
    .jobs = jobs, .job_capacity = 5U
};
#endif
static aStatus_t create(const aCommConfig_t *config, aCommHandle_t **handle)
{
#if ACOMM_STATIC_ENABLE
    *handle = &storage;
    return aCommInitStatic(config, *handle);
#else
    return aCommCreate(config, handle);
#endif
}
static aStatus_t destroy(aCommHandle_t *handle)
{
#if ACOMM_STATIC_ENABLE
    return aCommDeInitStatic(handle);
#else
    return aCommDestroy(handle);
#endif
}
int main(void)
{
    context_t state = {0};
    aCommHandle_t *handle;
    aCommConfig_t config;
    aCommConfigStructInit(&config);
    config.table = (aCommPlanConfig_t){entries, 5U, &ops, &state};
    config.retry_count = 1U;
    config.interval_ms = 1U;
    config.error_delay_ms = 2U;
    unsigned baseline = testAllocations();
    state.fail_prepare = A_TRUE;
    assert(create(&config, &handle) == A_STATUS_ERROR);
    assert(state.references == 0U && testAllocations() == baseline);
    state.fail_prepare = A_FALSE;
    testFailWaitCreate(A_TRUE);
    assert(create(&config, &handle) != A_STATUS_OK);
    assert(state.references == 0U && testAllocations() == baseline);
    testFailWaitCreate(A_FALSE);
    state.prepared = 0U;
    state.fail_once = A_TRUE;
    testSetTime(0U);
    assert(create(&config, &handle) == A_STATUS_OK);
    assert(state.references == 1U && state.prepared == 3U);
    assert(handle->config.job_count == 2U && handle->plan.count == 3U);
    unsigned active = testAllocations();
    aCommProcessResult_t result;
    assert(aCommProcess(handle, &result) == A_STATUS_OK);
    assert(state.seen[0] == 101U);
    assert(aCommProcess(handle, &result) == A_STATUS_OK);
    assert(state.seen[1] == 901U && !result.execution.advance);
    testAdvanceTime(1U);
    assert(aCommProcess(handle, &result) == A_STATUS_OK);
    assert(state.seen[2] == 103U);
    testAdvanceTime(1U);
    assert(aCommProcess(handle, &result) == A_STATUS_OK);
    assert(state.seen[3] == 901U && result.execution.advance);
    assert(aCommTrigger(handle) == A_STATUS_OK);
    assert(aCommProcess(handle, &result) == A_STATUS_OK);
    assert(state.seen[4] == 101U); /* 已结束的 ONCE 不被重新启动。 */
    assert(state.prepared == 3U && testAllocations() == active);
    state.busy_release = A_TRUE;
    assert(destroy(handle) == A_STATUS_BUSY);
    assert(handle->ready && state.references == 1U);
    state.busy_release = A_FALSE;
    assert(destroy(handle) == A_STATUS_OK);
    assert(state.references == 0U && testAllocations() == baseline);
    /* 无预编码回调也可执行；配置只包含禁用项时不分配周期作业。 */
    aCommTableOps_t callbacks = ops;
    callbacks.prepare = NULL;
    config.table.ops = &callbacks;
    config.table.items = &entries[1];
    config.table.count = 1U;
    assert(create(&config, &handle) == A_STATUS_OK);
    assert(handle->config.job_count == 0U && handle->plan.count == 0U);
    assert(aCommProcess(handle, &result) == A_STATUS_OK);
    assert(result.type == ACOMM_WORK_NONE);
    assert(destroy(handle) == A_STATUS_OK);
    callbacks.get = NULL;
    assert(create(&config, &handle) == A_STATUS_INVALID_PARAM);
    assert(testAllocations() == baseline);
    puts("Generic table/stride/groups/once/retry/preparation/cleanup passed");
    return 0;
}
