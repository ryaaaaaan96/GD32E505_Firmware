#include "aComm_mapping_internal.h"
#include <limits.h>
#include <string.h>
void aCommMappingClientSigRequestStructInit(
    aCommMappingClientSigRequest_t *request)
{
    if (request != NULL) {
        const aCommMappingClientSigRequest_t defaults = {
            .unit_id = 1U,
            .area = 0U,
            .target.paramIndex = ACOMM_MAP_SIG_WHOLE,
            .timeout = A_TIMEOUT_MS(1000U)
        };
        *request = defaults;
    }
}
static aStatus_t client_sig(aCommMapping_t *handle,
    const aCommMappingClientSigRequest_t *request, aBool_t write,
    const void *source)
{
    aCommValueInfo_t info;
    aCommMappingDataBuffer_t buffer;
    union {
        max_align_t alignment;
        uint8_t bytes[ACOMM_VALUE_CAPACITY];
    } value;
    void *protocol;
    aStatus_t status;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    if (request->result != NULL) *request->result = (aCommEndpointResult_t){0};
    status = aCommMappingOperationBegin(handle, ACOMM_MAPPING_CLIENT,
        request->timeout);
    if (status != A_STATUS_OK) return status;
    status = aCommMappingDataTargetInfo(handle, &request->target,
        request->area, request->word_order, &info);
    protocol = buffer.bytes;
    if (status == A_STATUS_OK) {
        status = aCommMappingAccessCheck(handle->config, request->unit_id,
            request->area, request->address,
            info.quantity, protocol, sizeof(buffer), write);
    }
    if (status == A_STATUS_OK && write) {
        if (source == NULL) {
            status = aCommMappingDataReadValue(handle, &request->target,
                                         value.bytes, info.size);
            source = value.bytes;
        }
        if (status == A_STATUS_OK) {
            status = aCommMappingValueEncode(handle, &info, request->area,
                request->word_order, source, protocol);
        }
        /* 即时新值也必须通过范围校验，不能依赖本地旧副本已校验。 */
        if (status == A_STATUS_OK) {
            status = aCommMappingValueDecode(handle, &info, request->area,
                request->word_order, protocol, value.bytes);
        }
    }
    if (status == A_STATUS_OK) {
        const aCommAddress_t target = {request->unit_id, request->area,
            request->address, info.quantity};
        if (write) {
            const aCommWriteAccess_t access = {
                &target, protocol, sizeof(buffer),
                aCommMappingRemaining(handle), request->result
            };
            status = aCommEndpointWrite(handle->config->protocol, &access);
        } else {
            const aCommReadAccess_t access = {
                &target, protocol, sizeof(buffer),
                aCommMappingRemaining(handle), request->result
            };
            status = aCommEndpointRead(handle->config->protocol, &access);
        }
        if (status == A_STATUS_OK && !write) {
            status = aCommMappingValueDecode(handle, &info, request->area,
                request->word_order, protocol, value.bytes);
            if (status == A_STATUS_OK) {
                status = aCommMappingDataWriteValue(handle, &request->target,
                                              value.bytes, info.size);
            }
        }
    }
    aAtomicStore(&handle->active, A_FALSE);
    return status;
}
aStatus_t aCommMappingClientReadSig(aCommMapping_t *handle,
    const aCommMappingClientSigRequest_t *request)
{
    return client_sig(handle, request, A_FALSE, NULL);
}
aStatus_t aCommMappingClientWriteSig(aCommMapping_t *handle,
    const aCommMappingClientSigRequest_t *request)
{
    return client_sig(handle, request, A_TRUE, NULL);
}
/* 总线通知只入队；消费者调用工作项时才按已登记映射执行 I/O。 */
static aStatus_t requested(void *context)
{
    aCommMappingSubscription_t *sub = context;
    aCommMapping_t *handle = sub->owner;
    const aCommMappingClientMap_t *map = sub->definition;
    const aCommNotifyInput_t *in = &sub->input;
    aCommDataInfo_t sig;
    aStatus_t status;
    const aCommDataQuery_t query = {map->target.deviceID, map->target.sigIndex};
    aBool_t write = in->event == ACOMM_REQUEST_WRITE;
    if (in->endpoint != handle->config->data ||
        (in->event != ACOMM_REQUEST_READ && !write) ||
        in->target.deviceID != query.deviceID ||
        in->target.sigIndex != query.sigIndex ||
        (map->access & (write ? ACOMM_MAP_ACCESS_WRITE :
                               ACOMM_MAP_ACCESS_READ)) == 0U) {
        return A_STATUS_UNSUPPORTED;
    }
    status = aCommDataInfo(in->endpoint, &query, &sig);
    if (status != A_STATUS_OK) return status;
    aCommSpan_t bound = {0U, sig.size};
    if (map->target.paramIndex != ACOMM_DATA_WHOLE) {
        if (map->target.paramIndex >= sig.param_count) {
            return A_STATUS_NOT_FOUND;
        }
        const aDataField_t *p = &sig.params[map->target.paramIndex];
        bound = (aCommSpan_t){p->offset, p->size};
    }
    if (in->overlap.size == 0U || in->overlap.offset < bound.offset ||
        in->overlap.offset - bound.offset > bound.size ||
        in->overlap.size > bound.size - (in->overlap.offset - bound.offset)) {
        return A_STATUS_INVALID_PARAM;
    }
    aCommMappingClientSigRequest_t access = {
        .unit_id = handle->client.unit_id, .area = map->area,
        .address = map->address,
        .target = ACOMM_MAP_LOCATION(map->target),
        .word_order = map->word_order,
        .timeout = handle->client.request_timeout
    };
    if (in->overlap.offset != bound.offset || in->overlap.size != bound.size) {
        /* 字段请求命中整组映射，仅发送该完整字段；不读旧值扩大写入。 */
        if (sig.type != ALIB_DATA_STRUCT ||
            map->target.paramIndex != ACOMM_DATA_WHOLE ||
            in->target.paramIndex >= sig.param_count) {
            return A_STATUS_UNSUPPORTED;
        }
        const aDataField_t *p = &sig.params[in->target.paramIndex];
        if (p->offset != in->overlap.offset || p->size != in->overlap.size) {
            return A_STATUS_UNSUPPORTED;
        }
        uint16_t offset;
        status = handle->config->protocol->ops->address->slice(&sig,
            in->target.paramIndex, &offset, &access.word_order);
        if (status != A_STATUS_OK) return status;
        if (offset > UINT16_MAX - map->address) return A_STATUS_INVALID_PARAM;
        access.target.paramIndex = in->target.paramIndex;
        access.address = (uint16_t)(map->address + offset);
    }
    const void *source = NULL;
    if (write) {
        if (in->data == NULL || in->overlap.offset < in->source.offset ||
            in->overlap.offset - in->source.offset > in->source.size ||
            in->overlap.size > in->source.size -
                (in->overlap.offset - in->source.offset)) {
            return A_STATUS_INVALID_PARAM;
        }
        source = (const uint8_t *)in->data +
                 in->overlap.offset - in->source.offset;
    }
    return client_sig(handle, &access, write, source);
}
void aCommMappingClientConfigStructInit(aCommMappingClientConfig_t *config)
{
    if (config != NULL) {
        const aCommMappingClientConfig_t defaults = {
            .unit_id = 1U, .request_timeout = A_TIMEOUT_MS(1000U)
        };
        *config = defaults;
        aCommMappingConfigStructInit(&config->binding);
    }
}
static void completed(void *context, aStatus_t status)
{
    aCommMappingSubscription_t *sub = context;
    const aCommNotifyInput_t input = sub->input;
    const aCommDataOps_t *ops = input.endpoint->ops->data;
    const aCommMappingClientMap_t *mapping = sub->definition;
    if (input.event == ACOMM_REQUEST_WRITE &&
        (mapping->target.publish_events & ACOMM_EVENT_WRITE_COMPLETE) != 0U) {
        const aCommPublishRequest_t publish = {
            .target = input.target,
            .event = ACOMM_EVENT_WRITE_COMPLETE, .status = status
        };
        (void)ops->publish(input.endpoint, &publish);
    }
    /* 先释放槽位，完成回调可以再次投递；之后不访问槽位。 */
    aAtomicStore(&sub->busy, A_FALSE);
    (void)ops->complete(input.delivery, status);
}
static aStatus_t notify(void *context, const aCommNotifyInput_t *input,
    aCommNotifyOutput_t *output)
{
    aCommMappingSubscription_t *sub = context;
    aBool_t expected = A_FALSE;
    if (input->delivery == NULL) return A_STATUS_UNSUPPORTED;
    if (!aAtomicCompareExchangeStrong(&sub->busy, &expected, A_TRUE)) {
        return A_STATUS_BUSY;
    }
    sub->input = *input;
    sub->work = (aCommRequest_t){requested, completed, sub};
    aStatus_t status = sub->owner->client.submit(
        sub->owner->client.submit_context, &sub->work);
    output->accepted = status == A_STATUS_OK;
    /* OK 后工作可能已完成且重新入队，不能再次读写槽位。 */
    if (!output->accepted) aAtomicStore(&sub->busy, A_FALSE);
    return status;
}
aStatus_t aCommMappingClientConfigCheck(
    const aCommMappingClientConfig_t *config)
{
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aCommMappingConfigCheck(&config->binding);
    if (status != A_STATUS_OK) return status;
    if (config->binding.protocol->ops->address->ready == NULL) {
        return A_STATUS_UNSUPPORTED;
    }
    status = config->binding.protocol->ops->address->ready(
        config->binding.protocol);
    if (status != A_STATUS_OK) return status;
    if ((config->maps == NULL) != (config->map_count == 0U) ||
        !aTimeoutIsValid(config->request_timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    for (size_t i = 0U; i < config->map_count; i++) {
        const aCommMappingClientMap_t *item = &config->maps[i];
        status = aCommMappingClientMapCheck(
            &config->binding, item, config->unit_id);
        if (status != A_STATUS_OK) return status;
        uint32_t mask = item->target.subscribe_events;
        /* UPDATED 不自动转成读/写，避免采集发布触发反馈循环。 */
        if (mask != ACOMM_SUBSCRIBE_MARK_ONLY &&
            (mask & ~(ACOMM_REQUEST_READ | ACOMM_REQUEST_WRITE)) != 0U) {
            return A_STATUS_UNSUPPORTED;
        }
        if (((mask & ACOMM_REQUEST_READ) != 0U &&
             (item->access & ACOMM_MAP_ACCESS_READ) == 0U) ||
            ((mask & ACOMM_REQUEST_WRITE) != 0U &&
             (item->access & ACOMM_MAP_ACCESS_WRITE) == 0U)) {
            return A_STATUS_INVALID_PARAM;
        }
        if (mask != 0U && mask != ACOMM_SUBSCRIBE_MARK_ONLY &&
            config->binding.data->ops->data->subscribe != NULL &&
            config->submit == NULL) return A_STATUS_INVALID_PARAM;
        for (size_t j = 0U; j < i; j++) {
            const aCommMappingClientMap_t *other = &config->maps[j];
            if ((mask & other->target.subscribe_events &
                 ACOMM_REQUEST_WRITE) != 0U && other->area == item->area &&
                item->address < (uint32_t)other->address + other->quantity &&
                other->address < (uint32_t)item->address + item->quantity) {
                return A_STATUS_INVALID_PARAM;
            }
        }
    }
    return A_STATUS_OK;
}
static size_t subscription_count(const aCommMappingClientConfig_t *config)
{
    size_t count = 0U;
    if (config->binding.data->ops->data->subscribe == NULL) return 0U;
    for (size_t i = 0U; i < config->map_count; i++) {
        if (config->maps[i].target.subscribe_events != 0U) count++;
    }
    return count;
}
static aStatus_t client_init(const aCommMappingClientConfig_t *config,
    aCommMapping_t *handle)
{
    aStatus_t status = aCommMappingStoragePrepare(handle,
        subscription_count(config));
    if (status != A_STATUS_OK) return status;
    handle->client = *config;
    status = aCommMappingCoreInit(handle, ACOMM_MAPPING_CLIENT);
    if (status != A_STATUS_OK) return status;
    for (size_t i = 0U; i < config->map_count; i++) {
        const aCommMappingClientMap_t *item = &config->maps[i];
        status = aCommMappingSubscribe(handle, &item->target, item, notify);
        if (status != A_STATUS_OK) {
            (void)aCommMappingCoreDeInit(handle);
            return status;
        }
    }
    return A_STATUS_OK;
}
#if ACOMM_STATIC_ENABLE
aStatus_t aCommMappingClientInitStatic(
    const aCommMappingClientConfig_t *config,
    aCommMapping_t *handle)
{
    aStatus_t status = aCommMappingClientConfigCheck(config);
    return status == A_STATUS_OK ? client_init(config, handle) : status;
}
#endif
#if ACOMM_DYNAMIC_ENABLE
aStatus_t aCommMappingClientCreate(const aCommMappingClientConfig_t *config,
    aCommMapping_t **handle_out)
{
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    aStatus_t status = aCommMappingClientConfigCheck(config);
    if (status != A_STATUS_OK) return status;
    aCommMapping_t *handle =
        aCommMappingAllocate(&config->binding, subscription_count(config));
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = client_init(config, handle);
    if (status != A_STATUS_OK) aOSFree(handle);
    else {
        handle->dynamic = A_TRUE;
        *handle_out = handle;
    }
    return status;
}
#endif
