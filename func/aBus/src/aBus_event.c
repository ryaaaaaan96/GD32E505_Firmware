#include "aBus_instance.h"
#if ABUS_EVENT_ENABLE
#include <limits.h>

/* 不接受原子库的隐藏锁；新平台须提供无锁 bool/unsigned 原语。 */
_Static_assert(A_ATOMIC_BOOL_LOCK_FREE == 2 && A_ATOMIC_INT_LOCK_FREE == 2,
               "aBus events require lock-free bool and unsigned atomics");

static aStatus_t locate(aBusHandle_t *bus, const aBusTarget_t *target,
    const aBusSig_t **sig, aBusSigState_t **state, aBusSpan_t *span)
{
    size_t offset = 0U;
    if (bus == NULL || target == NULL) return A_STATUS_INVALID_PARAM;
    if (bus->tables == NULL) return A_STATUS_NOT_READY;
    for (size_t t = 0U; t < bus->table_count; t++) {
        const aBusTable_t *table = &bus->tables[t];
        if (table->deviceID != target->deviceID) {
            offset += table->sig_count;
            continue;
        }
        if (target->sigIndex >= table->sig_count) return A_STATUS_NOT_FOUND;
        *sig = &table->sigs[target->sigIndex];
        *state = &bus->sigs[offset + target->sigIndex];
        if (target->paramIndex == ABUS_SIG_WHOLE) {
            *span = (aBusSpan_t){0U, (*sig)->size};
        } else {
            if (target->paramIndex >= (*sig)->param_count) {
                return A_STATUS_NOT_FOUND;
            }
            const aBusParam_t *p = &(*sig)->params[target->paramIndex];
            if (p->offset > (*sig)->size ||
                p->size > (*sig)->size - p->offset) {
                return A_STATUS_INVALID_PARAM;
            }
            *span = (aBusSpan_t){p->offset, p->size};
        }
        return A_STATUS_OK;
    }
    return A_STATUS_NOT_FOUND;
}

static aBusSubscriber_t **head(aBusSigState_t *state, size_t param)
{
    return param == ABUS_SIG_WHOLE ? &state->subscribers :
           &state->params[param].subscribers;
}

void aBusSubscriberStructInit(aBusSubscriber_t *node)
{
    if (node != NULL) {
        const aBusSubscriber_t empty = {0};
        *node = empty;
    }
}

void aBusSubscriberConfigStructInit(aBusSubscriberConfig_t *config)
{
    if (config != NULL) {
        const aBusSubscriberConfig_t defaults = {
            .event_mask = ABUS_EVENT_UPDATED, .type = ABUS_PROVIDER_APPLICATION
        };
        *config = defaults;
    }
}

void aBusPublishRequestStructInit(aBusPublishRequest_t *request)
{
    if (request != NULL) {
        const aBusPublishRequest_t defaults = {
            .target.paramIndex = ABUS_SIG_WHOLE, .event = ABUS_EVENT_UPDATED
        };
        *request = defaults;
    }
}

void aBusRequestConfigStructInit(aBusRequestConfig_t *config)
{
    if (config != NULL) {
        const aBusRequestConfig_t defaults = {
            .target.paramIndex = ABUS_SIG_WHOLE, .event = ABUS_REQUEST_READ
        };
        *config = defaults;
    }
}

void aBusRequestStructInit(aBusRequest_t *request,
    aBusDelivery_t *deliveries, size_t capacity)
{
    if (request == NULL) return;
    request->deliveries = deliveries;
    request->capacity = capacity;
    aBusRequestConfigStructInit(&request->config);
    aAtomicInit(&request->pending, 0U);
}

aBool_t aBusRequestDone(const aBusRequest_t *request)
{
    return request != NULL && aAtomicLoad(&request->pending) == 0U;
}

static aBool_t subscription_valid(uint32_t mask)
{
    return mask == ABUS_SUBSCRIBE_MARK_ONLY ||
           (mask != 0U && (mask & ~ABUS_ALL_EVENTS) == 0U);
}

aStatus_t aBusSubscribe(aBusHandle_t *bus, const aBusTarget_t *target,
    aBusSubscriber_t *node, const aBusSubscriberConfig_t *config)
{
    const aBusSig_t *sig;
    aBusSigState_t *state;
    aBusSpan_t span;
    aStatus_t status;
    if (node == NULL || config == NULL || config->owner == NULL ||
        config->type == ABUS_PROVIDER_ANY ||
        !subscription_valid(config->event_mask) ||
        (config->event_mask != ABUS_SUBSCRIBE_MARK_ONLY &&
         config->notify == NULL)) return A_STATUS_INVALID_PARAM;
    if (node->bus != NULL) return A_STATUS_BUSY;
    status = locate(bus, target, &sig, &state, &span);
    if (status != A_STATUS_OK) return status;
    if (aAtomicLoad(&bus->event_users) != 0U ||
        bus->subscriber_count == UINT_MAX - 1U) return A_STATUS_BUSY;
    node->config = *config;
    node->bus = bus;
    aBusSubscriber_t **list = head(state, target->paramIndex);
    node->list = list;
    node->next = *list;
    *list = node;
    bus->subscriber_count++;
    return A_STATUS_OK;
}

aStatus_t aBusUnsubscribe(aBusSubscriber_t *node)
{
    if (node == NULL) return A_STATUS_INVALID_PARAM;
    if (node->bus == NULL) return A_STATUS_NOT_READY;
    if (aAtomicLoad(&node->bus->event_users) != 0U) return A_STATUS_BUSY;
    aBusSubscriber_t **list = node->list;
    while (*list != NULL && *list != node) list = &(*list)->next;
    if (*list == NULL) return A_STATUS_NOT_FOUND;
    *list = node->next;
    node->bus->subscriber_count--;
    aBusSubscriberStructInit(node);
    return A_STATUS_OK;
}

aStatus_t aBusAssociationGet(aBusHandle_t *bus,
    const aBusAssociationQuery_t *query, aBusSubscriberConfig_t *info)
{
    const aBusSig_t *sig;
    aBusSigState_t *state;
    aBusSpan_t span;
    aStatus_t status;
    size_t index = 0U;
    if (query == NULL || info == NULL) return A_STATUS_INVALID_PARAM;
    status = locate(bus, &query->target, &sig, &state, &span);
    if (status != A_STATUS_OK) return status;
    for (const aBusSubscriber_t *n = *head(state, query->target.paramIndex);
         n != NULL; n = n->next) {
        if ((query->type != ABUS_PROVIDER_ANY &&
             query->type != n->config.type) ||
            (query->owner != NULL && query->owner != n->config.owner)) {
            continue;
        }
        if (index++ != query->index) continue;
        *info = n->config;
        return A_STATUS_OK;
    }
    return A_STATUS_NOT_FOUND;
}

aStatus_t aBusAssociationFind(aBusHandle_t *bus,
    const aBusAssociationQuery_t *query, aBusSubscriberConfig_t *info)
{
    aBusSubscriberConfig_t first, second;
    aBusAssociationQuery_t q;
    aStatus_t status;
    if (query == NULL || info == NULL) return A_STATUS_INVALID_PARAM;
    q = *query;
    q.index = 0U;
    status = aBusAssociationGet(bus, &q, &first);
    if (status != A_STATUS_OK) return status;
    q.index = 1U;
    status = aBusAssociationGet(bus, &q, &second);
    if (status == A_STATUS_OK) return A_STATUS_BUSY;
    if (status != A_STATUS_NOT_FOUND) return status;
    *info = first;
    return A_STATUS_OK;
}

/* 分发持有一个额外引用，防止第一个接收者提前完成后复用请求。 */
aStatus_t aBusComplete(aBusDelivery_t *delivery, aStatus_t status)
{
    aBusRequest_t *request;
    aBusHandle_t *bus;
    if (delivery == NULL) return A_STATUS_INVALID_PARAM;
    if (!aAtomicExchange(&delivery->pending, A_FALSE)) {
        return A_STATUS_NOT_READY;
    }
    request = delivery->request;
    bus = delivery->input.bus;
    if (request->config.complete != NULL) {
        request->config.complete(request->config.context, &delivery->input,
                                 status);
    }
    /* 此后不再使用节点/bus；最后一步才允许调用者回收请求。 */
    aAtomicFetchSub(&bus->event_users, 1U);
    aAtomicFetchSub(&request->pending, 1U);
    return A_STATUS_OK;
}

static void dispatch_list(aBusSubscriber_t *node, aBusNotifyInput_t *input,
    const void *owner, aBusRequest_t *request, aBusDispatchResult_t *result)
{
    size_t start = input->source.offset > input->binding.offset ?
                   input->source.offset : input->binding.offset;
    size_t end = input->source.offset + input->source.size;
    size_t bound_end = input->binding.offset + input->binding.size;
    if (end > bound_end) end = bound_end;
    if (start >= end) return;
    input->overlap = (aBusSpan_t){start, end - start};
    for (; node != NULL; node = node->next) {
        aBusNotifyOutput_t output = {.accepted = A_FALSE};
        aStatus_t status;
        aBusDelivery_t *delivery = NULL;
        if ((node->config.event_mask & input->event) == 0U ||
            (owner != NULL && owner != node->config.owner)) continue;
        size_t slot = result->matched++;
        input->subscriber = node;
        input->definition = node->config.definition;
        input->delivery = NULL;
        if (request != NULL && slot >= request->capacity) {
            status = A_STATUS_BUSY;
        } else {
            if (request != NULL) {
                delivery = &request->deliveries[slot];
                input->delivery = delivery;
                delivery->input = *input;
                delivery->request = request;
                aAtomicInit(&delivery->pending, A_TRUE);
                aAtomicFetchAdd(&request->pending, 1U);
                aAtomicFetchAdd(&input->bus->event_users, 1U);
            }
            status = node->config.notify(node->config.context,
                delivery != NULL ? &delivery->input : input, &output);
            if (delivery != NULL && !output.accepted) {
                /* 拒绝者没有后台工作，不产生完成回调。 */
                if (aAtomicExchange(&delivery->pending, A_FALSE)) {
                    aAtomicFetchSub(&input->bus->event_users, 1U);
                    aAtomicFetchSub(&request->pending, 1U);
                }
            }
        }
        if (output.accepted) result->accepted++;
        else {
            result->rejected++;
            if (status == A_STATUS_OK) status = A_STATUS_BUSY;
        }
        if (status != A_STATUS_OK && result->status == A_STATUS_OK) {
            result->status = status;
        }
    }
}

static aStatus_t dispatch(aBusHandle_t *bus, aBusNotifyInput_t *input,
    const void *owner, aBusRequest_t *request, aBusDispatchResult_t *result)
{
    const aBusSig_t *sig;
    aBusSigState_t *state;
    aStatus_t status = locate(bus, &input->target, &sig, &state,
                              &input->source);
    if (status != A_STATUS_OK) return status;
    if (request != NULL && request->config.event == ABUS_REQUEST_WRITE &&
        request->config.size != input->source.size) {
        return A_STATUS_INVALID_PARAM;
    }
    aAtomicFetchAdd(&bus->event_users, 1U);
    input->bus = bus;
    input->binding = (aBusSpan_t){0U, sig->size};
    dispatch_list(state->subscribers, input, owner, request, result);
    for (size_t i = 0U; i < sig->param_count; i++) {
        input->binding = (aBusSpan_t){sig->params[i].offset,
                                      sig->params[i].size};
        dispatch_list(state->params[i].subscribers, input, owner,
                      request, result);
    }
    aAtomicFetchSub(&bus->event_users, 1U);
    return result->status;
}

aStatus_t aBusPublish(aBusHandle_t *bus,
    const aBusPublishRequest_t *request, aBusDispatchResult_t *result)
{
    aBusDispatchResult_t local = {0};
    aStatus_t status;
    if (result != NULL) *result = local;
    if (request == NULL ||
        (request->event != ABUS_EVENT_UPDATED &&
         request->event != ABUS_EVENT_WRITE_COMPLETE)) {
        return A_STATUS_INVALID_PARAM;
    }
    aBusNotifyInput_t input = {
        .target = request->target, .event = request->event,
        .status = request->status
    };
    status = dispatch(bus, &input, request->owner, NULL, &local);
    local.status = status;
    if (result != NULL) *result = local;
    return status;
}

aStatus_t aBusRequest(aBusHandle_t *bus,
    const aBusRequestConfig_t *config, aBusRequest_t *request,
    aBusDispatchResult_t *result)
{
    aBusDispatchResult_t local = {0};
    unsigned expected = 0U;
    aStatus_t status;
    if (result != NULL) *result = local;
    if (config == NULL || request == NULL ||
        (request->capacity != 0U && request->deliveries == NULL) ||
        request->capacity > UINT_MAX - 1U ||
        (config->event != ABUS_REQUEST_READ &&
         config->event != ABUS_REQUEST_WRITE) ||
        (config->event == ABUS_REQUEST_WRITE && config->data == NULL) ||
        (config->event == ABUS_REQUEST_READ &&
         (config->data != NULL || config->size != 0U))) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!aAtomicCompareExchangeStrong(&request->pending, &expected, 1U)) {
        return A_STATUS_BUSY;
    }
    request->config = *config;
    aBusNotifyInput_t input = {
        .target = config->target, .event = config->event, .data = config->data
    };
    status = dispatch(bus, &input, config->owner, request, &local);
    local.status = status;
    if (result != NULL) *result = local;
    aAtomicFetchSub(&request->pending, 1U);
    return status;
}

aBool_t aBusNotificationFailed(aBusHandle_t *bus)
{
    return bus != NULL && bus->tables != NULL &&
           aAtomicExchange(&bus->notification_failed, A_FALSE);
}
#endif
