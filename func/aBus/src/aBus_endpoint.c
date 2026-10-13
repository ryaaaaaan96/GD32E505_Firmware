#include "aBus_endpoint.h"
_Static_assert(offsetof(aBusEndpoint_t, base) == 0U,
               "endpoint base must be first");
static aStatus_t read_value(aCommEndpoint_t *base,
    const aCommReadAccess_t *request)
{
    aBusEndpoint_t *endpoint = (aBusEndpoint_t *)base;
    const aCommTarget_t *target = request->target;
    if (target->paramIndex == ACOMM_DATA_WHOLE) {
        const aBusGetIndexRequest_t read = {
            .deviceID = target->deviceID, .sigIndex = target->sigIndex,
            .dst = request->data, .size = request->size,
            .timeout = request->timeout
        };
        return aBusGetByIndex(endpoint->bus, &read);
    }
    const aBusGetParamRequest_t read = {
        .deviceID = target->deviceID, .sigIndex = target->sigIndex,
        .paramIndex = target->paramIndex, .dst = request->data,
        .size = request->size, .timeout = request->timeout
    };
    return aBusGetParam(endpoint->bus, &read);
}

static aStatus_t write_value(aCommEndpoint_t *base,
    const aCommWriteAccess_t *request)
{
    aBusEndpoint_t *endpoint = (aBusEndpoint_t *)base;
    const aCommTarget_t *target = request->target;
    if (target->paramIndex == ACOMM_DATA_WHOLE) {
        const aBusSetIndexRequest_t write = {
            .deviceID = target->deviceID, .sigIndex = target->sigIndex,
            .src = request->data, .size = request->size,
            .timeout = request->timeout
        };
        return aBusSetByIndex(endpoint->bus, &write);
    }
    const aBusSetParamRequest_t write = {
        .deviceID = target->deviceID, .sigIndex = target->sigIndex,
        .paramIndex = target->paramIndex, .src = request->data,
        .size = request->size, .timeout = request->timeout
    };
    return aBusSetParam(endpoint->bus, &write);
}

static aStatus_t info_get(aCommEndpoint_t *base,
    const aCommDataQuery_t *query, aCommDataInfo_t *info)
{
    aBusEndpoint_t *endpoint = (aBusEndpoint_t *)base;
    const aBusSigQuery_t request = {query->deviceID, query->sigIndex};
    aBusSigInfo_t sig;
    aStatus_t status = aBusGetSigInfo(endpoint->bus, &request, &sig);
    if (status == A_STATUS_OK) {
        *info = (aCommDataInfo_t){sig.type, sig.size, sig.range,
            sig.params, sig.param_count};
    }
    return status;
}

static aStatus_t update_value(aCommEndpoint_t *base,
    const aCommDataUpdate_t *request)
{
    aBusEndpoint_t *endpoint = (aBusEndpoint_t *)base;
    const aBusUpdateRequest_t update = {
        .target = {request->target.deviceID, request->target.sigIndex},
        .buffer = request->buffer, .capacity = request->capacity,
        .edit = request->edit, .context = request->context,
        .timeout = request->timeout
    };
    return aBusUpdateSig(endpoint->bus, &update);
}
#if ABUS_EVENT_ENABLE
_Static_assert(ACOMM_EVENT_UPDATED == ABUS_EVENT_UPDATED &&
    ACOMM_EVENT_WRITE_COMPLETE == ABUS_EVENT_WRITE_COMPLETE &&
    ACOMM_REQUEST_READ == ABUS_REQUEST_READ &&
    ACOMM_REQUEST_WRITE == ABUS_REQUEST_WRITE &&
    ACOMM_SUBSCRIBE_MARK_ONLY == ABUS_SUBSCRIBE_MARK_ONLY,
    "event contract mismatch");
static aStatus_t notify(void *context, const aBusNotifyInput_t *input,
    aBusNotifyOutput_t *output)
{
    aBusEndpointSubscription_t *node = context;
    const aCommNotifyInput_t event = {
        .endpoint = &node->endpoint->base,
        .target = {input->target.deviceID, input->target.sigIndex,
                   input->target.paramIndex},
        .event = input->event,
        .source = {input->source.offset, input->source.size},
        .overlap = {input->overlap.offset, input->overlap.size},
        .data = input->data, .status = input->status,
        .delivery = input->delivery
    };
    aCommNotifyOutput_t result = {0};
    aStatus_t status = node->config.notify(node->config.context,
        &event, &result);
    output->accepted = result.accepted;
    return status;
}

static aStatus_t subscribe(aCommEndpoint_t *base,
    const aCommTarget_t *target, void *storage,
    const aCommSubscriptionConfig_t *config)
{
    aBusEndpointSubscription_t *node = storage;
    node->endpoint = (aBusEndpoint_t *)base;
    node->config = *config;
    aBusSubscriberStructInit(&node->node);
    const aBusTarget_t location = {
        target->deviceID, target->sigIndex, target->paramIndex
    };
    const aBusSubscriberConfig_t binding = {
        .event_mask = config->events, .type = config->type,
        .owner = config->owner, .definition = config->definition,
        .notify = notify, .context = node
    };
    return aBusSubscribe(node->endpoint->bus, &location, &node->node,
        &binding);
}

static aStatus_t unsubscribe(void *storage)
{
    aBusEndpointSubscription_t *node = storage;
    return aBusUnsubscribe(&node->node);
}

static aStatus_t publish(aCommEndpoint_t *base,
    const aCommPublishRequest_t *request)
{
    aBusEndpoint_t *endpoint = (aBusEndpoint_t *)base;
    const aBusPublishRequest_t publish = {
        .target = {request->target.deviceID, request->target.sigIndex,
                   request->target.paramIndex},
        .event = request->event, .status = request->status
    };
    return aBusPublish(endpoint->bus, &publish, NULL);
}

static aStatus_t complete(void *delivery, aStatus_t status)
{
    return aBusComplete(delivery, status);
}
#endif
static const aCommDataOps_t data_ops = {
    .info = info_get, .update = update_value,
#if ABUS_EVENT_ENABLE
    .subscription_size = sizeof(aBusEndpointSubscription_t),
    .subscribe = subscribe, .unsubscribe = unsubscribe,
    .publish = publish, .complete = complete,
#endif
};
static const aCommEndpointOps_t endpoint_ops = {
    .read = read_value, .write = write_value, .data = &data_ops
};
aStatus_t aBusEndpointInit(aBusEndpoint_t *endpoint, aBusHandle_t *bus)
{
    if (endpoint == NULL || bus == NULL) return A_STATUS_INVALID_PARAM;
    *endpoint = (aBusEndpoint_t){.base.ops = &endpoint_ops, .bus = bus};
    return A_STATUS_OK;
}
