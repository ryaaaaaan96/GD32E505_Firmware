#include "aComm_mapping_internal.h"
#include <string.h>
void aCommMappingConfigStructInit(aCommMappingConfig_t *config)
{
    if (config != NULL) *config = (aCommMappingConfig_t){0};
}
void aCommMappingSigTargetStructInit(aCommMappingSigTarget_t *target)
{
    if (target != NULL) {
        const aCommMappingSigTarget_t defaults = {
            .paramIndex = ACOMM_MAP_SIG_WHOLE
        };
        *target = defaults;
    }
}
void aCommMappingSigReadRequestStructInit(
    aCommMappingSigReadRequest_t *request)
{
    if (request != NULL) {
        const aCommMappingSigReadRequest_t defaults = {
            .target.paramIndex = ACOMM_MAP_SIG_WHOLE,
            .timeout = A_TIMEOUT_NO_WAIT
        };
        *request = defaults;
    }
}
void aCommMappingSigWriteRequestStructInit(
    aCommMappingSigWriteRequest_t *request)
{
    if (request != NULL) {
        const aCommMappingSigWriteRequest_t defaults = {
            .target.paramIndex = ACOMM_MAP_SIG_WHOLE,
            .timeout = A_TIMEOUT_NO_WAIT
        };
        *request = defaults;
    }
}
aTimeout_t aCommMappingRemaining(aCommMapping_t *handle)
{
    return aTimepointRemaining(&handle->deadline, aOSGetUptimeMs());
}
aStatus_t aCommMappingOperationBegin(aCommMapping_t *handle,
    aCommMappingRole_t role, aTimeout_t timeout)
{
    if (handle == NULL || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (handle->role != role) return A_STATUS_UNSUPPORTED;
    if (aAtomicExchange(&handle->active, A_TRUE)) return A_STATUS_BUSY;
    handle->deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    return A_STATUS_OK;
}
aStatus_t aCommMappingConfigCheck(const aCommMappingConfig_t *config)
{
    if (config == NULL || config->data == NULL || config->protocol == NULL ||
        config->data->ops == NULL || config->protocol->ops == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    const aCommDataOps_t *data = config->data->ops->data;
    const aCommAddressOps_t *address = config->protocol->ops->address;
    if (data == NULL || data->info == NULL || data->update == NULL ||
        config->data->ops->read == NULL || config->data->ops->write == NULL ||
        address == NULL || address->area == NULL || address->check == NULL ||
        address->describe == NULL || address->layout == NULL ||
        address->copy == NULL || address->slice == NULL) {
        return A_STATUS_UNSUPPORTED;
    }
    if ((address->prepare == NULL) != (address->read_prepared == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (data->subscribe != NULL && (data->unsubscribe == NULL ||
        data->complete == NULL || data->publish == NULL ||
        data->subscription_size == 0U)) return A_STATUS_INVALID_PARAM;
    return A_STATUS_OK;
}
aStatus_t aCommMappingStoragePrepare(aCommMapping_t *handle,
    size_t subscriptions)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->ready) return A_STATUS_BUSY;
    aCommMappingSubscription_t *storage = handle->subscriptions;
    size_t capacity = handle->subscription_capacity;
    if (subscriptions > capacity || (subscriptions != 0U && storage == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    memset(handle, 0, sizeof(*handle));
    handle->subscriptions = storage;
    handle->subscription_capacity = capacity;
    aAtomicInit(&handle->active, A_FALSE);
    return A_STATUS_OK;
}
aStatus_t aCommMappingCoreInit(aCommMapping_t *handle,
    aCommMappingRole_t role)
{
    handle->role = role;
    handle->config = role == ACOMM_MAPPING_CLIENT ?
        &handle->client.binding : &handle->server.binding;
    if (handle->config->snapshot == NULL) {
        handle->config->snapshot = handle->snapshot;
        handle->config->snapshot_size = sizeof(handle->snapshot);
    }
    handle->ready = A_TRUE;
    return A_STATUS_OK;
}
aStatus_t aCommMappingSubscribe(aCommMapping_t *handle,
    const aCommTargetConfig_t *target, const void *definition,
    aCommNotifyFn_t notify)
{
    const aCommDataOps_t *ops = handle->config->data->ops->data;
    if (target->subscribe_events == 0U || ops->subscribe == NULL) {
        return A_STATUS_OK;
    }
    if (handle->subscription_count == handle->subscription_capacity) {
        return A_STATUS_INVALID_PARAM;
    }
    aCommMappingSubscription_t *sub =
        &handle->subscriptions[handle->subscription_count];
    void *native = sub->native;
    if (native == NULL) return A_STATUS_INVALID_PARAM;
    memset(sub, 0, sizeof(*sub));
    sub->native = native;
    sub->definition = definition;
    aAtomicInit(&sub->busy, A_FALSE);
    sub->owner = handle;
    const aCommSubscriptionConfig_t config = {
        .events = target->subscribe_events,
        .type = handle->config->protocol->ops->address->provider_type,
        .owner = handle, .definition = definition,
        .notify = notify, .context = sub
    };
    const aCommTarget_t location = ACOMM_MAP_LOCATION(*target);
    aStatus_t status = ops->subscribe(handle->config->data, &location,
        sub->native, &config);
    if (status == A_STATUS_OK) handle->subscription_count++;
    return status;
}

/* 停止阶段调用；已有请求完成前不拆节点，也不影响应用调度器。 */
aStatus_t aCommMappingCoreDeInit(aCommMapping_t *handle)
{
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (aAtomicLoad(&handle->active) || handle->plan_count != 0U) {
        return A_STATUS_BUSY;
    }
    for (size_t i = 0U; i < handle->subscription_count; i++) {
        if (aAtomicLoad(&handle->subscriptions[i].busy)) return A_STATUS_BUSY;
    }
    while (handle->subscription_count != 0U) {
        aStatus_t status = handle->config->data->ops->data->unsubscribe(
            handle->subscriptions[handle->subscription_count - 1U].native);
        if (status != A_STATUS_OK) return status;
        handle->subscription_count--;
    }
    handle->ready = A_FALSE;
    return A_STATUS_OK;
}
#if ACOMM_STATIC_ENABLE
aStatus_t aCommMappingDeInitStatic(aCommMapping_t *handle)
{
    if (handle == NULL || handle->dynamic) return A_STATUS_INVALID_PARAM;
    return aCommMappingCoreDeInit(handle);
}
#endif
#if ACOMM_DYNAMIC_ENABLE
aCommMapping_t *aCommMappingAllocate(const aCommMappingConfig_t *config,
    size_t subscriptions)
{
    const size_t align = _Alignof(max_align_t);
    const size_t native_size = config->data->ops->data->subscription_size;
    if (native_size > SIZE_MAX - align) return NULL;
    size_t stride = (native_size + align - 1U) / align * align;
    size_t head = (sizeof(aCommMapping_t) + align - 1U) / align * align;
    size_t slot = sizeof(aCommMappingSubscription_t);
    slot = (slot + align - 1U) / align * align;
    if (stride > SIZE_MAX - slot) return NULL;
    stride += slot;
    if (subscriptions > (SIZE_MAX - head) / stride) return NULL;
    aCommMapping_t *handle = aOSAlloc(head + subscriptions * stride);
    if (handle == NULL) return NULL;
    memset(handle, 0, sizeof(*handle));
    handle->subscriptions = (void *)((uint8_t *)handle + head);
    handle->subscription_capacity = subscriptions;
    /* 通用槽连续，原生节点起点及步长均满足标准最大对齐。 */
    size_t nodes = head + subscriptions * sizeof(*handle->subscriptions);
    nodes = (nodes + align - 1U) / align * align;
    for (size_t i = 0U; i < subscriptions; i++) {
        handle->subscriptions[i].native = (uint8_t *)handle + nodes +
            i * (stride - slot);
    }
    return handle;
}
aStatus_t aCommMappingDestroy(aCommMapping_t *handle)
{
    if (handle == NULL) return A_STATUS_OK;
    if (!handle->dynamic) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aCommMappingCoreDeInit(handle);
    if (status == A_STATUS_OK) aOSFree(handle);
    return status;
}
#endif
