/* 不链接具体协议、总线或 OS：验证数据绑定的独立复用及请求借用生命周期。 */
#include "aComm_instance.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    aCommSubscriptionConfig_t config;
    aCommTarget_t target;
    aBool_t used;
} node_t;
typedef struct {
    aCommEndpoint_t base;
    uint32_t values[2];
    unsigned subscriptions, observations, publications, executions, completions;
    aBool_t updating, fail_edit, reject_queue, inline_queue, block_unsubscribe;
    aCommRequest_t queued;
    aBool_t pending;
    aStatus_t completed_status;
    aCommBinding_t *binding;
    node_t *node;
    aBool_t resubmit;
} fixture_t;
static const aDataField_t fields[] = {
    {.offset = 0U, .size = 4U, .type = ALIB_DATA_U32},
    {.offset = 4U, .size = 4U, .type = ALIB_DATA_U32}
};
static aStatus_t info(aCommEndpoint_t *base, const aCommDataQuery_t *query,
    aCommDataInfo_t *out)
{
    (void)base;
    assert(query->deviceID == 7U && query->sigIndex == 2U);
    *out = (aCommDataInfo_t){.type = ALIB_DATA_STRUCT, .size = 8U,
        .params = fields, .param_count = 2U};
    return A_STATUS_OK;
}
static aStatus_t read_data(aCommEndpoint_t *base,
    const aCommReadAccess_t *request)
{
    fixture_t *fixture = (void *)base;
    const aCommTarget_t *target = request->target;
    size_t offset = target->paramIndex == ACOMM_DATA_WHOLE ? 0U :
        target->paramIndex * 4U;
    assert(offset + request->size <= sizeof(fixture->values));
    memcpy(request->data, (uint8_t *)fixture->values + offset, request->size);
    return A_STATUS_OK;
}
static aStatus_t write_data(aCommEndpoint_t *base,
    const aCommWriteAccess_t *request)
{
    fixture_t *fixture = (void *)base;
    const aCommTarget_t *target = request->target;
    size_t offset = target->paramIndex == ACOMM_DATA_WHOLE ? 0U :
        target->paramIndex * 4U;
    assert(offset + request->size <= sizeof(fixture->values));
    memcpy((uint8_t *)fixture->values + offset, request->data, request->size);
    return A_STATUS_OK;
}
static aStatus_t update(aCommEndpoint_t *base, const aCommDataUpdate_t *request)
{
    fixture_t *fixture = (void *)base;
    assert(!fixture->updating && request->capacity >= sizeof(fixture->values));
    fixture->updating = A_TRUE;
    memcpy(request->buffer, fixture->values, sizeof(fixture->values));
    aStatus_t status = request->edit(request->context, request->buffer,
        sizeof(fixture->values));
    if (status == A_STATUS_OK) {
        memcpy(fixture->values, request->buffer, sizeof(fixture->values));
    }
    fixture->updating = A_FALSE;
    return status;
}
static aStatus_t subscribe(aCommEndpoint_t *base, const aCommTarget_t *target,
    void *storage, const aCommSubscriptionConfig_t *config)
{
    fixture_t *fixture = (void *)base;
    node_t *node = storage;
    *node = (node_t){.config = *config, .target = *target, .used = A_TRUE};
    fixture->subscriptions++;
    fixture->node = node;
    assert(config->owner == fixture && config->type == 55U);
    return A_STATUS_OK;
}
static aStatus_t unsubscribe(void *storage)
{
    node_t *node = storage;
    fixture_t *fixture = (void *)node->config.owner;
    if (fixture->block_unsubscribe) return A_STATUS_BUSY;
    assert(node->used && fixture->subscriptions != 0U);
    node->used = A_FALSE;
    fixture->subscriptions--;
    return A_STATUS_OK;
}
static aStatus_t publish(aCommEndpoint_t *base,
    const aCommPublishRequest_t *request)
{
    fixture_t *fixture = (void *)base;
    assert(request->event == ACOMM_EVENT_WRITE_COMPLETE);
    assert(aCommBindingDeInit(fixture->binding) == A_STATUS_BUSY);
    fixture->publications++;
    return A_STATUS_OK;
}
static aStatus_t emit(fixture_t *fixture, uint32_t event, size_t parameter,
    const void *value, size_t offset, size_t size)
{
    const aCommNotifyInput_t input = {
        .endpoint = &fixture->base, .target = {7U, 2U, parameter},
        .event = event, .source = {offset, size}, .overlap = {offset, size},
        .data = value,
        .delivery = event == ACOMM_REQUEST_WRITE ||
            event == ACOMM_REQUEST_READ ? fixture : NULL
    };
    aCommNotifyOutput_t output = {0};
    node_t *node = fixture->node;
    assert(node->used);
    aStatus_t status = node->config.notify(node->config.context, &input, &output);
    assert(output.accepted == (status == A_STATUS_OK));
    return status;
}
static aStatus_t complete(void *delivery, aStatus_t status)
{
    fixture_t *fixture = delivery;
    fixture->completions++;
    fixture->completed_status = status;
    if (fixture->resubmit) {
        fixture->resubmit = A_FALSE;
        assert(emit(fixture, ACOMM_REQUEST_READ, 1U, NULL, 4U, 4U) ==
            A_STATUS_OK);
    }
    return A_STATUS_OK;
}
static const aCommDataOps_t data_ops = {
    .info = info, .update = update, .subscription_size = sizeof(node_t),
    .subscribe = subscribe, .unsubscribe = unsubscribe,
    .publish = publish, .complete = complete
};
static const aCommEndpointOps_t ops = {
    .read = read_data, .write = write_data, .data = &data_ops
};
static aStatus_t execute(void *context, const aCommBindingRequest_t *request)
{
    fixture_t *fixture = context;
    assert(aCommBindingDeInit(fixture->binding) == A_STATUS_BUSY);
    assert(request->entry->deviceID == 7U && request->info.size == 8U);
    assert(request->target.paramIndex == 1U);
    if (request->write) {
        uint32_t value;
        memcpy(&value, request->source, sizeof(value));
        assert(value == 123U);
    } else assert(request->source == NULL);
    fixture->executions++;
    return A_STATUS_OK;
}
static aStatus_t submit(void *context, const aCommRequest_t *work)
{
    fixture_t *fixture = context;
    if (fixture->reject_queue || fixture->pending) return A_STATUS_BUSY;
    if (fixture->inline_queue) {
        aCommRequest_t copy = *work;
        aStatus_t status = copy.execute(copy.context);
        copy.complete(copy.context, status);
    } else {
        fixture->queued = *work;
        fixture->pending = A_TRUE;
    }
    return A_STATUS_OK;
}
static void drain(fixture_t *fixture)
{
    assert(fixture->pending);
    aCommRequest_t work = fixture->queued;
    fixture->pending = A_FALSE;
    aStatus_t status = work.execute(work.context);
    work.complete(work.context, status);
}
static void observe(void *context, const aCommEntry_t *entry,
    const aCommNotifyInput_t *input)
{
    fixture_t *fixture = context;
    assert(entry->deviceID == 7U && input->delivery == NULL);
    fixture->observations++;
}
static aStatus_t edit(void *context, void *snapshot, size_t size)
{
    fixture_t *fixture = context;
    assert(fixture->updating && size == 8U);
    uint32_t *values = snapshot;
    values[0] += 1U;
    values[1] += 2U;
    return fixture->fail_edit ? A_STATUS_INVALID_PARAM : A_STATUS_OK;
}
static aStatus_t inspect(void *context, void *snapshot, size_t size)
{
    fixture_t *fixture = context;
    assert(!fixture->updating && size == 8U);
    assert(memcmp(snapshot, fixture->values, size) == 0);
    return A_STATUS_OK;
}
int main(void)
{
    fixture_t fixture = {.base.ops = &ops, .values = {1U, 2U}};
    const aCommBindingConfig_t config = {
        .data = &fixture.base, .request_capacity = 1U
    };
    const aCommBindingEvents_t events = {
        .type = 55U, .context = &fixture, .execute = execute,
        .observe = observe, .submit = submit, .submit_context = &fixture
    };
    const aCommEntry_t entry = {
        .deviceID = 7U, .sigIndex = 2U, .paramIndex = ACOMM_DATA_WHOLE,
        .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
        .subscribe_events = ACOMM_REQUEST_READ | ACOMM_REQUEST_WRITE |
                            ACOMM_EVENT_UPDATED,
        .publish_events = ACOMM_EVENT_WRITE_COMPLETE
    };
    aCommBinding_t binding = {0};
    fixture.binding = &binding;
    _Alignas(max_align_t) uint8_t storage[1024];
    size_t size;
    assert(aCommBindingStorageSize(&config, SIZE_MAX, &size) ==
        A_STATUS_INVALID_PARAM);
    aCommBindingConfig_t oversized = config;
    oversized.request_capacity = SIZE_MAX;
    assert(aCommBindingStorageSize(&oversized, 2U, &size) ==
        A_STATUS_INVALID_PARAM);
    assert(aCommBindingStorageSize(&config, 2U, &size) == A_STATUS_OK);
    assert(size <= sizeof(storage));
    assert(aCommBindingStorageSetup(&binding, &config, storage, 2U) ==
        A_STATUS_OK);
    for (size_t i = 0U; i < 2U; i++) {
        assert((uintptr_t)binding.subscriptions[i].native %
            _Alignof(max_align_t) == 0U);
    }
    assert(aCommBindingInit(&config, &events, &binding) == A_STATUS_OK);
    assert(aCommBindingInit(&config, &events, &binding) == A_STATUS_BUSY);
    assert(aCommBindingSubscribe(&binding, &entry) == A_STATUS_OK);
    uint32_t value = 123U;
    fixture.reject_queue = A_TRUE;
    assert(emit(&fixture, ACOMM_REQUEST_WRITE, 1U, &value, 4U, 4U) ==
        A_STATUS_BUSY);
    assert(!fixture.pending && fixture.executions == 0U);
    fixture.reject_queue = A_FALSE;
    assert(emit(&fixture, ACOMM_REQUEST_WRITE, 1U, &value, 4U, 4U) ==
        A_STATUS_OK);
    assert(emit(&fixture, ACOMM_REQUEST_WRITE, 1U, &value, 4U, 4U) ==
        A_STATUS_BUSY);
    assert(aCommBindingDeInit(&binding) == A_STATUS_BUSY);
    node_t *first_node = fixture.node;
    aCommEntry_t other = entry;
    other.paramIndex = 0U;
    other.subscribe_events = ACOMM_REQUEST_READ;
    assert(aCommBindingSubscribe(&binding, &other) == A_STATUS_OK);
    /* 不同订阅也受共享池容量约束；拒绝后不会占用该订阅。 */
    assert(emit(&fixture, ACOMM_REQUEST_READ, 0U, NULL, 0U, 4U) ==
           A_STATUS_BUSY);
    assert(!aAtomicLoad(&binding.subscriptions[1].busy));
    fixture.node = first_node;
    fixture.resubmit = A_TRUE;
    drain(&fixture);
    assert(fixture.pending && fixture.publications == 1U);
    drain(&fixture);
    assert(fixture.executions == 2U && fixture.completions == 2U);
    fixture.inline_queue = A_TRUE;
    assert(emit(&fixture, ACOMM_REQUEST_READ, 1U, NULL, 4U, 4U) == A_STATUS_OK);
    assert(fixture.executions == 3U && fixture.completions == 3U);
    /* 半个字段不能扩大成完整字段执行，错误也必须完成已接收请求。 */
    assert(emit(&fixture, ACOMM_REQUEST_READ, 1U, NULL, 4U, 2U) == A_STATUS_OK);
    assert(fixture.executions == 3U && fixture.completions == 4U);
    assert(fixture.completed_status == A_STATUS_UNSUPPORTED);
    assert(emit(&fixture, ACOMM_EVENT_UPDATED, 1U, NULL, 4U, 4U) ==
           A_STATUS_OK);
    assert(fixture.observations == 1U && !fixture.pending);

    aCommBindingTransfer_t transfer = {
        .target = {7U, 2U}, .size = 8U, .edit = edit, .context = &fixture,
        .write = A_TRUE, .timeout = A_TIMEOUT_NO_WAIT
    };
    fixture.fail_edit = A_TRUE;
    assert(aCommBindingTransfer(&binding, &transfer) == A_STATUS_INVALID_PARAM);
    assert(fixture.values[0] == 1U && fixture.values[1] == 2U);
    fixture.fail_edit = A_FALSE;
    assert(aCommBindingTransfer(&binding, &transfer) == A_STATUS_OK);
    assert(fixture.values[0] == 2U && fixture.values[1] == 4U);
    transfer.write = A_FALSE;
    transfer.edit = inspect;
    assert(aCommBindingTransfer(&binding, &transfer) == A_STATUS_OK);
    aCommValueReadRequest_t read;
    aCommValueReadRequestStructInit(&read);
    read.target = (aCommTarget_t){7U, 2U, 1U};
    read.data = &value;
    read.size = sizeof(value);
    assert(aCommBindingRead(&binding, &read) == A_STATUS_OK && value == 4U);
    fixture.block_unsubscribe = A_TRUE;
    assert(aCommBindingDeInit(&binding) == A_STATUS_BUSY && binding.ready);
    fixture.block_unsubscribe = A_FALSE;
    assert(aCommBindingDeInit(&binding) == A_STATUS_OK);
    assert(fixture.subscriptions == 0U);
    assert(aCommBindingRead(&binding, &read) == A_STATUS_NOT_READY);

    /* 观察/标记不需要请求池；按实际能力布置存储。 */
    aCommBindingConfig_t observer_config = config;
    observer_config.request_capacity = 0U;
    size_t observer_size;
    assert(aCommBindingStorageSize(&observer_config, 2U, &observer_size) ==
        A_STATUS_OK);
    assert(observer_size < size);
    assert(aCommBindingStorageSetup(&binding, &observer_config, storage, 2U) ==
        A_STATUS_OK);
    assert(binding.requests == NULL);
    assert(aCommBindingInit(&observer_config, &events, &binding) ==
           A_STATUS_OK);
    aCommEntry_t marker = entry;
    marker.subscribe_events = 0U;
    assert(aCommBindingSubscribe(&binding, &marker) == A_STATUS_OK);
    assert(binding.subscription_count == 0U);
    marker.subscribe_events = ACOMM_SUBSCRIBE_MARK_ONLY;
    assert(aCommBindingSubscribe(&binding, &marker) == A_STATUS_OK);
    assert(binding.subscription_count == 1U);
    aCommEntry_t observer = entry;
    observer.subscribe_events = ACOMM_EVENT_UPDATED;
    assert(aCommBindingSubscribe(&binding, &observer) == A_STATUS_OK);
    assert(emit(&fixture, ACOMM_EVENT_UPDATED, 1U, NULL, 4U, 4U) ==
           A_STATUS_OK);
    assert(fixture.observations == 2U);
    assert(aCommBindingDeInit(&binding) == A_STATUS_OK);
    puts("Protocol-independent binding: events/lifetime/snapshot/storage passed");
    return 0;
}
