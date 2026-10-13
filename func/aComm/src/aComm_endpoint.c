#include "aComm_endpoint.h"

aStatus_t aCommEndpointRead(aCommEndpoint_t *endpoint,
    const aCommReadAccess_t *request)
{
    if (endpoint == NULL || endpoint->ops == NULL || request == NULL ||
        request->target == NULL || request->data == NULL ||
        !aTimeoutIsValid(request->timeout)) return A_STATUS_INVALID_PARAM;
    if (endpoint->ops->read == NULL) return A_STATUS_UNSUPPORTED;
    return endpoint->ops->read(endpoint, request);
}
aStatus_t aCommEndpointWrite(aCommEndpoint_t *endpoint,
    const aCommWriteAccess_t *request)
{
    if (endpoint == NULL || endpoint->ops == NULL || request == NULL ||
        request->target == NULL || request->data == NULL ||
        !aTimeoutIsValid(request->timeout)) return A_STATUS_INVALID_PARAM;
    if (endpoint->ops->write == NULL) return A_STATUS_UNSUPPORTED;
    return endpoint->ops->write(endpoint, request);
}
aStatus_t aCommDataInfo(aCommEndpoint_t *endpoint,
    const aCommDataQuery_t *query, aCommDataInfo_t *info)
{
    if (endpoint == NULL || endpoint->ops == NULL || query == NULL ||
        info == NULL) return A_STATUS_INVALID_PARAM;
    if (endpoint->ops->data == NULL || endpoint->ops->data->info == NULL) {
        return A_STATUS_UNSUPPORTED;
    }
    return endpoint->ops->data->info(endpoint, query, info);
}
aStatus_t aCommDataUpdate(aCommEndpoint_t *endpoint,
    const aCommDataUpdate_t *request)
{
    if (endpoint == NULL || endpoint->ops == NULL || request == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (endpoint->ops->data == NULL || endpoint->ops->data->update == NULL) {
        return A_STATUS_UNSUPPORTED;
    }
    return endpoint->ops->data->update(endpoint, request);
}
aStatus_t aCommDataRead(aCommEndpoint_t *endpoint,
    const aCommDataRead_t *request)
{
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    const aCommTarget_t target = {
        request->deviceID, request->sigIndex, request->paramIndex
    };
    const aCommReadAccess_t access = {
        .target = &target, .data = request->dst, .size = request->size,
        .timeout = request->timeout
    };
    return aCommEndpointRead(endpoint, &access);
}
aStatus_t aCommDataWrite(aCommEndpoint_t *endpoint,
    const aCommDataWrite_t *request)
{
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    const aCommTarget_t target = {
        request->deviceID, request->sigIndex, request->paramIndex
    };
    const aCommWriteAccess_t access = {
        .target = &target, .data = request->src, .size = request->size,
        .timeout = request->timeout
    };
    return aCommEndpointWrite(endpoint, &access);
}
aStatus_t aCommTargetConfigCheck(const aCommTargetConfig_t *target)
{
    if (target == NULL) return A_STATUS_INVALID_PARAM;
    const uint32_t all = ACOMM_EVENT_UPDATED | ACOMM_EVENT_WRITE_COMPLETE |
        ACOMM_REQUEST_READ | ACOMM_REQUEST_WRITE;
    if ((target->subscribe_events != ACOMM_SUBSCRIBE_MARK_ONLY &&
         (target->subscribe_events & ~all) != 0U) ||
        (target->publish_events & ~ACOMM_EVENT_WRITE_COMPLETE) != 0U) {
        return A_STATUS_INVALID_PARAM;
    }
    return A_STATUS_OK;
}
