#include "aBus_instance.h"
#include "aOS.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t x, y; } pair_t;
static pair_t data;
static const aBusRange_t range = {.min.u32 = 0U, .max.u32 = 10U};
static const aBusParam_t params[] = {
    {.offset = 0U, .size = 4U, .type = ALIB_DATA_U32, .range = &range},
    {.offset = 4U, .size = 4U, .type = ALIB_DATA_U32},
    {.offset = 0U, .size = 4U, .type = ALIB_DATA_U32}
};
static const aBusSig_t sigs[] = {
    {.sigKey = 1U, .flags = ABUS_SIG_FLAG_LOCK, .type = ALIB_DATA_STRUCT,
     .size = sizeof(pair_t), .params = params, .param_count = 3U}
};
static const aBusTable_t table = {.deviceID = 9U, .sigs = sigs, .sig_count = 1U};
ABUS_RAM_BIND_EXPORT(binding, 66U, 9U, 0U, data);
static aBool_t fail_unlock;
aStatus_t __real_aOSMutexUnlock(aOSMutex_t mutex);
aStatus_t __wrap_aOSMutexUnlock(aOSMutex_t mutex)
{
    aStatus_t status = __real_aOSMutexUnlock(mutex);
    return fail_unlock && status == A_STATUS_OK ? A_STATUS_ERROR : status;
}

static unsigned identity;
static unsigned received;
static aAtomicUint_t completed;
static aAtomicBool_t concurrent;
static A_ATOMIC_TYPE(aBusDelivery_t *) completion_slot;
static aBusSpan_t overlap;
static aBusDelivery_t *saved[8];
static size_t saved_count;
static aBool_t reject, immediate;
static aBusRequest_t *current;
static aBusHandle_t *bus;

static aStatus_t notify(void *context, const aBusNotifyInput_t *input,
    aBusNotifyOutput_t *output)
{
    (void)context;
    received++;
    overlap = input->overlap;
    if (reject) return A_STATUS_BUSY;
    output->accepted = A_TRUE;
    if (input->delivery != NULL) {
        if (aAtomicLoad(&concurrent)) {
            /* 单独消费者可以在分发未结束时完成；发布者从不等待它。 */
            aBusDelivery_t *empty = NULL;
            if (!aAtomicCompareExchangeStrong(&completion_slot, &empty,
                                                input->delivery)) {
                output->accepted = A_FALSE;
                return A_STATUS_BUSY;
            }
        } else if (immediate) {
            assert(aBusComplete(input->delivery, A_STATUS_OK) == A_STATUS_OK);
            assert(!aBusRequestDone(current));
            assert(aBusComplete(input->delivery, A_STATUS_OK) ==
                   A_STATUS_NOT_READY);
        } else saved[saved_count++] = input->delivery;
    }
    /* 如果自动通知仍持有数据锁，这里读会失败。 */
    pair_t value;
    aBusGetIndexRequest_t get = {
        .deviceID = 9U, .sigIndex = 0U, .dst = &value, .size = sizeof(value),
        .timeout = A_TIMEOUT_NO_WAIT
    };
    assert(aBusGetByIndex(input->bus, &get) == A_STATUS_OK);
    return A_STATUS_OK;
}
static void done(void *context, const aBusNotifyInput_t *input, aStatus_t status)
{
    (void)context;
    assert(status == A_STATUS_OK);
    assert(input->definition == &identity);
    assert(!aBusRequestDone(current));
    aAtomicFetchAdd(&completed, 1U);
}
static aStatus_t edit(void *context, void *buffer, size_t size)
{
    (void)context;
    assert(size == sizeof(pair_t));
    ((pair_t *)buffer)->x++;
    return A_STATUS_OK;
}

static void *consumer(void *unused)
{
    (void)unused;
    for (;;) {
        aBusDelivery_t *delivery = aAtomicExchange(&completion_slot, NULL);
        if (delivery != NULL) {
            assert(aBusComplete(delivery, A_STATUS_OK) == A_STATUS_OK);
        } else if (!aAtomicLoad(&concurrent)) break;
    }
    return NULL;
}

static aAtomicUint_t published;
static aStatus_t publication(void *context, const aBusNotifyInput_t *input,
    aBusNotifyOutput_t *output)
{
    (void)context;
    assert(input->event == ABUS_EVENT_UPDATED);
    aAtomicFetchAdd(&published, 1U);
    output->accepted = A_TRUE;
    return A_STATUS_OK;
}
static void *publisher(void *unused)
{
    (void)unused;
    const aBusPublishRequest_t request = {
        .target = {9U, 0U, ABUS_SIG_WHOLE}, .event = ABUS_EVENT_UPDATED
    };
    for (unsigned i = 0U; i < 1000U; i++) {
        assert(aBusPublish(bus, &request, NULL) == A_STATUS_OK);
    }
    return NULL;
}

int main(void)
{
#if ABUS_DYNAMIC_ENABLE
    assert(aBusCreate(66U, &table, 1U, &bus) == A_STATUS_OK);
#else
    aBusHandle_t instance;
    aBusSigState_t states[1];
    aBusParamState_t fields[3];
    aBusInstanceStructInit(&instance, states, 1U);
    assert(aBusInitStatic(66U, &table, 1U, &instance) == A_STATUS_INVALID_PARAM);
    instance.params = fields;
    instance.param_capacity = 3U;
    assert(aBusInitStatic(66U, &table, 1U, &instance) == A_STATUS_OK);
    bus = &instance;
#endif
    aBusSubscriber_t nodes[5];
    for (size_t i = 0U; i < 5U; i++) aBusSubscriberStructInit(&nodes[i]);
    aBusSubscriberConfig_t config = {
        .event_mask = ABUS_EVENT_UPDATED | ABUS_REQUEST_WRITE,
        .type = ABUS_PROVIDER_APPLICATION, .owner = &identity,
        .definition = &identity, .notify = notify
    };
    const aBusTarget_t whole = {9U, 0U, ABUS_SIG_WHOLE};
    const aBusTarget_t x = {9U, 0U, 0U};
    const aBusTarget_t y = {9U, 0U, 1U};
    const aBusTarget_t alias = {9U, 0U, 2U};
    assert(aBusSubscribe(bus, &whole, &nodes[0], &config) == A_STATUS_OK);
    assert(aBusSubscribe(bus, &x, &nodes[1], &config) == A_STATUS_OK);
    assert(aBusSubscribe(bus, &y, &nodes[2], &config) == A_STATUS_OK);
    assert(aBusSubscribe(bus, &alias, &nodes[3], &config) == A_STATUS_OK);
    assert(aBusSubscribe(bus, &x, &nodes[0], &config) == A_STATUS_BUSY);
    config.event_mask = ABUS_SUBSCRIBE_MARK_ONLY;
    config.notify = NULL;
    assert(aBusSubscribe(bus, &whole, &nodes[4], &config) == A_STATUS_OK);
#if ABUS_DYNAMIC_ENABLE
    aBusHandle_t *other;
    aBusSubscriber_t other_node;
    aBusSubscriberStructInit(&other_node);
    assert(aBusCreate(67U, &table, 1U, &other) == A_STATUS_OK);
    assert(other->sigs[0].params != bus->sigs[0].params);
    assert(aBusSubscribe(other, &whole, &nodes[0], &config) == A_STATUS_BUSY);
    assert(aBusSubscribe(other, &whole, &other_node, &config) == A_STATUS_OK);
    const aBusPublishRequest_t other_publish = {
        .target = whole, .event = ABUS_EVENT_UPDATED
    };
    aBusDispatchResult_t other_result;
    assert(aBusPublish(other, &other_publish, &other_result) == A_STATUS_OK);
    assert(other_result.matched == 0U && received == 0U);
    assert(aBusUnsubscribe(&other_node) == A_STATUS_OK);
    assert(aBusDestroy(other) == A_STATUS_OK);
#endif
    aBusAssociationQuery_t query = {.target = whole};
    aBusSubscriberConfig_t info;
    assert(aBusAssociationFind(bus, &query, &info) == A_STATUS_BUSY);
    query.index = 0U;
    assert(aBusAssociationGet(bus, &query, &info) == A_STATUS_OK);
    assert(info.event_mask == ABUS_SUBSCRIBE_MARK_ONLY);
    pair_t value = {5U, 7U};
    aBusSetIndexRequest_t set = {
        .deviceID = 9U, .sigIndex = 0U, .src = &value, .size = sizeof(value),
        .timeout = A_TIMEOUT_NO_WAIT
    };
    assert(aBusSetByIndex(bus, &set) == A_STATUS_OK && received == 4U);
    received = 0U;
    aBusSetParamRequest_t field = {
        .deviceID = 9U, .sigIndex = 0U, .paramIndex = 0U,
        .src = &value.x, .size = 4U, .timeout = A_TIMEOUT_NO_WAIT
    };
    assert(aBusSetParam(bus, &field) == A_STATUS_OK && received == 3U);
    assert(overlap.offset == 0U && overlap.size == 4U);
    aBusUpdateRequest_t update = {
        .target = {9U, 0U}, .buffer = &value, .capacity = sizeof(value),
        .edit = edit, .timeout = A_TIMEOUT_NO_WAIT
    };
    received = 0U;
    assert(aBusUpdateSig(bus, &update) == A_STATUS_OK && received == 4U);
    value.x = 99U;
    unsigned prior_notifications = received;
    assert(aBusSetByIndex(bus, &set) == A_STATUS_INVALID_PARAM);
    assert(received == prior_notifications);
    value.x = 5U;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    fail_unlock = A_TRUE;
    assert(aBusSetByIndex(bus, &set) == A_STATUS_ERROR);
    fail_unlock = A_FALSE;
    assert(received == prior_notifications && data.x == 5U);
#endif
    aBusSetKeyRequest_t keyed = {
        .deviceID = 9U, .sigKey = 1U, .src = &value, .size = sizeof(value),
        .timeout = A_TIMEOUT_NO_WAIT
    };
    assert(aBusSetByKey(bus, &keyed) == A_STATUS_OK);
    assert(received == prior_notifications + 4U);
    reject = A_TRUE;
    assert(aBusSetByIndex(bus, &set) == A_STATUS_OK);
    assert(aBusNotificationFailed(bus));
    assert(!aBusNotificationFailed(bus));
    reject = A_FALSE;

    aBusRequest_t request;
    aBusDelivery_t deliveries[4];
    aBusDispatchResult_t result;
    aBusRequestStructInit(&request, deliveries, 4U);
    current = &request;
    aBusRequestConfig_t access = {
        .target = whole, .event = ABUS_REQUEST_WRITE, .data = &value,
        .size = sizeof(value), .complete = done
    };
    immediate = A_TRUE;
    assert(aBusRequest(bus, &access, &request, &result) == A_STATUS_OK);
    assert(result.accepted == 4U && aAtomicLoad(&completed) == 4U);
    assert(aBusRequestDone(&request));
    immediate = A_FALSE;
    access.target = x;
    access.data = &value.x;
    access.size = sizeof(value.x);
    assert(aBusRequest(bus, &access, &request, &result) == A_STATUS_OK);
    assert(result.accepted == 3U && !aBusRequestDone(&request));
    assert(aBusUnsubscribe(&nodes[0]) == A_STATUS_BUSY);
    assert(aBusRequest(bus, &access, &request, &result) == A_STATUS_BUSY);
    for (size_t i = 0U; i < saved_count; i++) {
        assert(saved[i]->input.overlap.size == 4U);
        assert(aBusComplete(saved[i], A_STATUS_OK) == A_STATUS_OK);
    }
    assert(aBusRequestDone(&request));
    saved_count = 0U;
    request.capacity = 1U;
    assert(aBusRequest(bus, &access, &request, &result) == A_STATUS_BUSY);
    assert(result.matched == 3U && result.accepted == 1U && result.rejected == 2U);
    assert(aBusComplete(saved[0], A_STATUS_OK) == A_STATUS_OK);
    assert(aBusRequestDone(&request));
    access.owner = &access;
    assert(aBusRequest(bus, &access, &request, &result) == A_STATUS_OK);
    assert(result.matched == 0U && aBusRequestDone(&request));
    aBusPublishRequest_t publish = {.target = whole,
        .event = ABUS_SUBSCRIBE_MARK_ONLY};
    assert(aBusPublish(bus, &publish, &result) == A_STATUS_INVALID_PARAM);
    aBusTargetConfig_t policy = {.subscribe_events =
        ABUS_SUBSCRIBE_MARK_ONLY | ABUS_EVENT_UPDATED};
    assert(aBusTargetConfigCheck(&policy) == A_STATUS_INVALID_PARAM);
    policy.subscribe_events = 0U;
    policy.publish_events = ABUS_EVENT_UPDATED;
    assert(aBusTargetConfigCheck(&policy) == A_STATUS_INVALID_PARAM);
    /* 请求槽由两个任务交接，完成与继续分发并发；每次等待只在测试调用者。 */
    pthread_t worker;
    aAtomicInit(&completion_slot, NULL);
    aAtomicStore(&concurrent, A_TRUE);
    assert(pthread_create(&worker, NULL, consumer, NULL) == 0);
    access.owner = NULL;
    request.capacity = 4U;
    for (unsigned round = 0U; round < 1000U; round++) {
        aStatus_t status = aBusRequest(bus, &access, &request, &result);
        assert(status == A_STATUS_OK || status == A_STATUS_BUSY);
        assert(result.accepted + result.rejected == 3U);
        while (!aBusRequestDone(&request)) { }
    }
    aAtomicStore(&concurrent, A_FALSE);
    assert(pthread_join(worker, NULL) == 0);
    for (size_t i = 0U; i < 5U; i++) {
        assert(aBusUnsubscribe(&nodes[i]) == A_STATUS_OK);
    }
    config.event_mask = ABUS_EVENT_UPDATED;
    config.notify = publication;
    assert(aBusSubscribe(bus, &whole, &nodes[0], &config) == A_STATUS_OK);
    pthread_t writers[2];
    for (size_t i = 0U; i < 2U; i++) {
        assert(pthread_create(&writers[i], NULL, publisher, NULL) == 0);
    }
    for (size_t i = 0U; i < 2U; i++) assert(pthread_join(writers[i], NULL) == 0);
    assert(aAtomicLoad(&published) == 2000U);
    assert(aBusUnsubscribe(&nodes[0]) == A_STATUS_OK);
#if ABUS_DYNAMIC_ENABLE
    assert(aBusDestroy(bus) == A_STATUS_OK);
#else
    assert(aBusDeInitStatic(bus) == A_STATUS_OK);
#endif
    return 0;
}
