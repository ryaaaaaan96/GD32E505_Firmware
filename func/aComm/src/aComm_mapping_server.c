#include "aComm_mapping_internal.h"
#include <string.h>
void aCommMappingServerMapStructInit(aCommMappingServerMap_t *map)
{
    if (map != NULL) {
        const aCommMappingServerMap_t defaults = {
            .area = 0U,
            .quantity = 0U,
            .flags = ACOMM_MAP_ACCESS_READ | ACOMM_MAP_ACCESS_WRITE,
            .target.paramIndex = ACOMM_MAP_SIG_WHOLE
        };
        *map = defaults;
    }
}
void aCommMappingAddressRangeStructInit(aCommMappingAddressRange_t *range)
{
    if (range != NULL) {
        const aCommMappingAddressRange_t defaults = {
            .area = 0U,
            .flags = ACOMM_MAP_ACCESS_READ | ACOMM_MAP_ACCESS_WRITE
        };
        *range = defaults;
    }
}
void aCommMappingServerConfigStructInit(aCommMappingServerConfig_t *config)
{
    if (config != NULL) {
        const aCommMappingServerConfig_t defaults = {0};
        *config = defaults;
        aCommMappingConfigStructInit(&config->binding);
    }
}
static aStatus_t notify(void *context, const aCommNotifyInput_t *input,
    aCommNotifyOutput_t *output)
{
    aCommMappingSubscription_t *sub = context;
    (void)output;
    sub->owner->server.notify(sub->owner->server.notify_context,
        sub->definition, input);
    return A_STATUS_OK;
}
aStatus_t aCommMappingServerConfigCheck(
    const aCommMappingServerConfig_t *config)
{
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aCommMappingConfigCheck(&config->binding);
    if (status != A_STATUS_OK) return status;
    if (config->range_count == 0U && config->map_count == 0U) {
        return A_STATUS_INVALID_PARAM;
    }
    return aCommMappingDataDefinitionsCheck(config);
}
static size_t subscription_count(const aCommMappingServerConfig_t *config)
{
    size_t count = 0U;
    if (config->binding.data->ops->data->subscribe == NULL) return 0U;
    for (size_t i = 0U; i < config->map_count; i++) {
        if (config->maps[i].target.subscribe_events != 0U) count++;
    }
    return count;
}
static aStatus_t server_init(const aCommMappingServerConfig_t *config,
    aCommMapping_t *handle)
{
    aStatus_t status = aCommMappingStoragePrepare(handle,
        subscription_count(config));
    if (status != A_STATUS_OK) return status;
    handle->server = *config;
    status = aCommMappingCoreInit(handle, ACOMM_MAPPING_SERVER);
    if (status != A_STATUS_OK) return status;
    for (size_t i = 0U; i < config->map_count; i++) {
        const aCommMappingServerMap_t *item = &config->maps[i];
        status = aCommMappingSubscribe(handle, &item->target, item, notify);
        if (status != A_STATUS_OK) {
            (void)aCommMappingCoreDeInit(handle);
            return status;
        }
    }
    return A_STATUS_OK;
}
#if ACOMM_STATIC_ENABLE
aStatus_t aCommMappingServerInitStatic(
    const aCommMappingServerConfig_t *config,
    aCommMapping_t *handle)
{
    aStatus_t status = aCommMappingServerConfigCheck(config);
    return status == A_STATUS_OK ? server_init(config, handle) : status;
}
#endif
#if ACOMM_DYNAMIC_ENABLE
aStatus_t aCommMappingServerCreate(const aCommMappingServerConfig_t *config,
    aCommMapping_t **handle_out)
{
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    aStatus_t status = aCommMappingServerConfigCheck(config);
    if (status != A_STATUS_OK) return status;
    aCommMapping_t *handle =
        aCommMappingAllocate(&config->binding, subscription_count(config));
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = server_init(config, handle);
    if (status != A_STATUS_OK) aOSFree(handle);
    else {
        handle->dynamic = A_TRUE;
        *handle_out = handle;
    }
    return status;
}
#endif
aStatus_t aCommMappingServerRead(void *context,
    const aCommAddressReadRequest_t *request)
{
    aCommMapping_t *handle = context;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aCommMappingOperationBegin(handle,
        ACOMM_MAPPING_SERVER, request->timeout);
    if (status != A_STATUS_OK) return status;
    status = aCommMappingDataRead(handle, request);
    aAtomicStore(&handle->active, A_FALSE);
    return status;
}
aStatus_t aCommMappingServerWrite(void *context,
    const aCommAddressWriteRequest_t *request)
{
    aCommMapping_t *handle = context;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aCommMappingOperationBegin(handle,
        ACOMM_MAPPING_SERVER, request->timeout);
    if (status != A_STATUS_OK) return status;
    status = aCommMappingDataWrite(handle, request);
    aAtomicStore(&handle->active, A_FALSE);
    return status;
}
