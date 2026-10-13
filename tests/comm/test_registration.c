/* 假协议验证统一注册，不链接 aModbus、aBus 或设备驱动。 */
#include "aComm_instance.h"
#include <assert.h>
#include <stdio.h>

void testAdvanceTime(uint32_t ms);
void testFailWaitCreate(aBool_t fail);
void testFailAllocation(aBool_t fail);
unsigned testAllocations(void);
void aOSDelayMs(uint32_t ms) { testAdvanceTime(ms); }

typedef struct {
    aCommEntry_t common;
    unsigned token;
    char extra[13];
} item_t;
static const item_t items[] = {
    {.common.poll_time_ms = ACOMM_POLL_ONCE, .token = 42U}
};
typedef struct {
    aBool_t table;
} config_t;
typedef struct {
    unsigned initialized, released, processed, references;
    aBool_t live, fail_init, fail_prepare, busy_release;
} context_t;
static const aCommEntry_t *entry(const void *array, size_t index)
{
    return &((const item_t *)array)[index].common;
}
static aStatus_t acquire(void *argument)
{
    context_t *context = argument;
    assert(context->live);
    context->references++;
    return A_STATUS_OK;
}
static aStatus_t release(void *argument)
{
    context_t *context = argument;
    assert(context->references == 1U && context->live);
    context->references--;
    return A_STATUS_OK;
}
static aStatus_t prepare(void *argument, const aCommBatch_t *batch,
    aCommPrepared_t *prepared)
{
    (void)batch;
    (void)prepared;
    return ((context_t *)argument)->fail_prepare ? A_STATUS_ERROR : A_STATUS_OK;
}
static void execute(void *argument, const aCommBatch_t *batch,
    const aCommPrepared_t *prepared, aCommStepResult_t *result,
    aCommEndpointResult_t *response)
{
    (void)prepared;
    (void)response;
    context_t *context = argument;
    assert(context->live && context->references == 1U);
    assert(batch->count == 1U);
    assert(((const item_t *)batch->entries)[batch->indices[0]].token == 42U);
    context->processed++;
    result->status = A_STATUS_OK;
    result->advance = A_TRUE;
}
static const aCommTableOps_t table_ops = {
    .get = entry, .execute = execute, .prepare = prepare,
    .acquire = acquire, .release = release
};
static aStatus_t check(const aCommProtocol_t *protocol)
{
    context_t *context = protocol->context;
    if (protocol->config == NULL || context == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    return context->live ? A_STATUS_BUSY : A_STATUS_OK;
}
static aStatus_t init(const aCommProtocol_t *protocol, aCommPlanConfig_t *table)
{
    const config_t *config = protocol->config;
    context_t *context = protocol->context;
    assert(!context->live);
    context->initialized++;
    if (context->fail_init) return A_STATUS_ERROR;
    context->live = A_TRUE;
    if (config->table) {
        *table = (aCommPlanConfig_t){
            .items = protocol->items, .count = protocol->count,
            .ops = &table_ops, .context = context
        };
    }
    return A_STATUS_OK;
}
static void process(void *argument, size_t step, aCommStepResult_t *result)
{
    context_t *context = argument;
    assert(context->live && step == 0U);
    context->processed++;
    result->status = A_STATUS_OK;
    result->advance = A_TRUE;
}
static aStatus_t deinit(void *argument)
{
    context_t *context = argument;
    assert(context->live && context->references == 0U);
    if (context->busy_release) return A_STATUS_BUSY;
    context->released++;
    context->live = A_FALSE;
    return A_STATUS_OK;
}
static const aCommProtocolOps_t ops = {
    .check = check, .init = init, .process = process, .deinit = deinit
};
typedef struct {
    aCommHandle_t handle;
    aCommPlanFrame_t frame;
    size_t index;
    aCommTableJob_t group;
    aCommJobState_t job;
} storage_t;
static aStatus_t create(const aCommConfig_t *config, storage_t *storage,
    aBool_t dynamic, aCommHandle_t **handle)
{
    (void)storage;
    (void)dynamic;
#if ACOMM_DYNAMIC_ENABLE
    if (dynamic) return aCommCreate(config, handle);
#endif
#if ACOMM_STATIC_ENABLE
    storage->handle = (aCommHandle_t){
        .plan = {.frames = &storage->frame, .capacity = 1U,
                 .indices = &storage->index, .index_capacity = 1U},
        .table_jobs = &storage->group, .table_capacity = 1U,
        .jobs = &storage->job, .job_capacity = 1U
    };
    aStatus_t status = aCommInitStatic(config, &storage->handle);
    *handle = status == A_STATUS_OK ? &storage->handle : NULL;
    return status;
#else
    return A_STATUS_UNSUPPORTED;
#endif
}
static aStatus_t destroy(aCommHandle_t *handle)
{
#if ACOMM_DYNAMIC_ENABLE
    if (handle->dynamic) return aCommDestroy(handle);
#endif
#if ACOMM_STATIC_ENABLE
    return aCommDeInitStatic(handle);
#else
    return A_STATUS_UNSUPPORTED;
#endif
}
static void run(aBool_t dynamic, aBool_t table)
{
    storage_t storage;
    config_t protocol = {.table = table};
    context_t context = {0};
    aCommHandle_t *handle = NULL;
    aCommProcessResult_t result;
    aCommConfig_t config = {
        .protocol = {.config = &protocol, .items = items, .count = 1U,
                     .ops = &ops, .context = &context},
        .error_delay_ms = 5U
    };
    unsigned allocations = testAllocations();
    assert(aCommConfigCheck(&config) == A_STATUS_OK);
    assert(context.initialized == 0U);
    context.fail_init = A_TRUE;
    assert(create(&config, &storage, dynamic, &handle) == A_STATUS_ERROR);
    assert(!context.live && context.released == 0U);
    context.fail_init = A_FALSE;
    testFailWaitCreate(A_TRUE);
    assert(create(&config, &storage, dynamic, &handle) == A_STATUS_NO_MEMORY);
    testFailWaitCreate(A_FALSE);
    assert(!context.live && context.released == 1U);
    assert(testAllocations() == allocations);
    if (table) {
        context.fail_prepare = A_TRUE;
        assert(create(&config, &storage, dynamic, &handle) == A_STATUS_ERROR);
        assert(!context.live && context.references == 0U);
        context.fail_prepare = A_FALSE;
        assert(testAllocations() == allocations);
    }
#if ACOMM_DYNAMIC_ENABLE
    if (dynamic) {
        testFailAllocation(A_TRUE);
        assert(create(&config, &storage, dynamic, &handle) ==
               A_STATUS_NO_MEMORY);
        testFailAllocation(A_FALSE);
        assert(!context.live);
    }
#endif
    assert(create(&config, &storage, dynamic, &handle) == A_STATUS_OK);
    assert(aCommProcess(handle, &result) == A_STATUS_OK);
    assert(context.processed == 1U && result.execution.status == A_STATUS_OK);
    assert(aCommConfigCheck(&config) == A_STATUS_BUSY);
    context.busy_release = A_TRUE;
    assert(destroy(handle) == A_STATUS_BUSY);
    assert(context.references == 0U && context.live);
    assert(aCommProcess(handle, &result) == A_STATUS_NOT_READY);
    assert(aCommTrigger(handle) == A_STATUS_NOT_READY);
    context.busy_release = A_FALSE;
    assert(destroy(handle) == A_STATUS_OK);
    assert(!context.live && testAllocations() == allocations);
    /* 销毁失败后可重试；销毁成功后可用同一注册环境重新初始化。 */
    assert(create(&config, &storage, dynamic, &handle) == A_STATUS_OK);
    assert(destroy(handle) == A_STATUS_OK);
    assert(testAllocations() == allocations);
}
int main(void)
{
#if ACOMM_STATIC_ENABLE
    run(A_FALSE, A_FALSE);
    run(A_FALSE, A_TRUE);
#endif
#if ACOMM_DYNAMIC_ENABLE
    run(A_TRUE, A_FALSE);
    run(A_TRUE, A_TRUE);
#endif
    puts("Generic registration: service/table/init/rollback/release passed");
    return 0;
}
