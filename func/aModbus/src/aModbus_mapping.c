#include "aModbus_mapping_internal.h"
#include <limits.h>
#include <string.h>

/* -------------------- 映射生命周期与共享校验 -------------------- */

void aModbusMappingConfigStructInit(aModbusMappingConfig_t *config)
{
    if (config != NULL) *config = (aModbusMappingConfig_t){0};
}
aTimeout_t aModbusMappingRemaining(aModbusMapping_t *handle)
{
    return aTimepointRemaining(&handle->deadline, aOSGetUptimeMs());
}
static aStatus_t mapping_begin(aModbusMapping_t *handle,
    aModbusMappingRole_t role, aTimeout_t timeout)
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
aStatus_t aModbusMappingConfigCheck(const aModbusMappingConfig_t *config)
{
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aModbusMapEndpointCheck(config->protocol);
    return status == A_STATUS_OK ?
        aCommBindingConfigCheck(&config->comm) : status;
}
static aStatus_t storage_prepare(aModbusMapping_t *handle,
    size_t subscriptions, size_t maps)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->ready) return A_STATUS_BUSY;
    aCommBindingSubscription_t *storage = handle->comm.subscriptions;
    size_t capacity = handle->comm.subscription_capacity;
    aCommBindingRequestSlot_t *requests = handle->comm.requests;
    size_t request_capacity = handle->comm.request_capacity;
    if (subscriptions > capacity || (subscriptions != 0U && storage == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    aModbusMapLink_t *links = handle->links;
    size_t map_capacity = handle->map_capacity;
    if (maps > map_capacity || (maps != 0U && links == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    memset(handle, 0, sizeof(*handle));
    handle->links = links;
    handle->map_capacity = map_capacity;
    handle->comm.subscriptions = storage;
    handle->comm.subscription_capacity = capacity;
    handle->comm.requests = requests;
    handle->comm.request_capacity = request_capacity;
    aAtomicInit(&handle->active, A_FALSE);
    return A_STATUS_OK;
}
static void links_init(aModbusMapping_t *handle)
{
    const aBool_t client = handle->role == AMODBUS_MAPPING_CLIENT;
    const size_t count = client ? handle->client.map_count :
        handle->server.map_count;
    for (size_t i = 0U; i < count; i++) {
        const aCommEntry_t *entry = client ? &handle->client.maps[i].comm :
            &handle->server.maps[i].comm;
        uint16_t area = client ? handle->client.maps[i].area :
            handle->server.maps[i].area;
        handle->links[i] = (aModbusMapLink_t){SIZE_MAX, SIZE_MAX};
        for (size_t j = i; j-- > 0U;) {
            const aCommEntry_t *other = client ? &handle->client.maps[j].comm :
                &handle->server.maps[j].comm;
            uint16_t other_area = client ? handle->client.maps[j].area :
                handle->server.maps[j].area;
            if (entry->deviceID != other->deviceID ||
                entry->sigIndex != other->sigIndex || area != other_area ||
                (client && entry->poll_time_ms != other->poll_time_ms)) {
                continue;
            }
            handle->links[i].previous = j;
            handle->links[j].next = i;
            break;
        }
    }
}
static aStatus_t mapping_init(aModbusMapping_t *handle,
    aModbusMappingRole_t role, const aCommBindingEvents_t *events)
{
    handle->role = role;
    links_init(handle);
    handle->config = role == AMODBUS_MAPPING_CLIENT ?
        &handle->client.binding : &handle->server.binding;
    aStatus_t status = aCommBindingInit(&handle->config->comm, events,
        &handle->comm);
    if (status != A_STATUS_OK) return status;
    handle->ready = A_TRUE;
    return A_STATUS_OK;
}
/* 停止阶段调用；协议事务和计划由本模块检查，订阅生命周期交给 aComm。 */
static aStatus_t mapping_deinit(aModbusMapping_t *handle)
{
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (aAtomicLoad(&handle->active) || handle->plan_count != 0U) {
        return A_STATUS_BUSY;
    }
    aStatus_t status = aCommBindingDeInit(&handle->comm);
    if (status == A_STATUS_OK) handle->ready = A_FALSE;
    return status;
}
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusMappingDeInitStatic(aModbusMapping_t *handle)
{
    if (handle == NULL || handle->dynamic) return A_STATUS_INVALID_PARAM;
    return mapping_deinit(handle);
}
#endif
#if AMODBUS_DYNAMIC_ENABLE
static aModbusMapping_t *mapping_allocate(const aModbusMappingConfig_t *config,
    size_t subscriptions, size_t maps)
{
    size_t size;
    if (aCommBindingStorageSize(&config->comm, subscriptions, &size) !=
        A_STATUS_OK || size > SIZE_MAX - sizeof(aModbusMapping_t)) return NULL;
    const size_t align = _Alignof(max_align_t);
    if (maps > (SIZE_MAX - (align - 1U)) / sizeof(aModbusMapLink_t))
        return NULL;
    size_t binding_offset = maps * sizeof(aModbusMapLink_t);
    binding_offset = (binding_offset + align - 1U) / align * align;
    if (binding_offset > SIZE_MAX - sizeof(aModbusMapping_t) - size)
        return NULL;
    aModbusMapping_t *handle =
        aOSAlloc(sizeof(*handle) + binding_offset + size);
    if (handle == NULL) return NULL;
    *handle = (aModbusMapping_t){
        .links = (void *)(handle + 1), .map_capacity = maps
    };
    aStatus_t status = aCommBindingStorageSetup(&handle->comm, &config->comm,
        (uint8_t *)(handle + 1) + binding_offset, subscriptions);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return NULL;
    }
    return handle;
}
aStatus_t aModbusMappingDestroy(aModbusMapping_t *handle)
{
    if (handle == NULL) return A_STATUS_OK;
    if (!handle->dynamic) return A_STATUS_INVALID_PARAM;
    aStatus_t status = mapping_deinit(handle);
    if (status == A_STATUS_OK) aOSFree(handle);
    return status;
}
#endif

/* -------------------- 主站映射与事件执行 -------------------- */

void aModbusMappingClientSigRequestStructInit(
    aModbusMappingClientSigRequest_t *request)
{
    if (request != NULL) {
        const aModbusMappingClientSigRequest_t defaults = {
            .unit_id = 1U,
            .area = 0U,
            .target.paramIndex = ACOMM_MAP_SIG_WHOLE,
            .timeout = A_TIMEOUT_MS(1000U)
        };
        *request = defaults;
    }
}
static aStatus_t client_sig(aModbusMapping_t *handle,
    const aModbusMappingClientSigRequest_t *request, aBool_t write,
    const void *source)
{
    aModbusMapValue_t info;
    aModbusMappingDataBuffer_t buffer;
    union {
        max_align_t alignment;
        uint8_t bytes[ACOMM_VALUE_CAPACITY];
    } value;
    void *protocol;
    aStatus_t status;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    if (request->result != NULL) *request->result = (aCommEndpointResult_t){0};
    status = mapping_begin(handle, AMODBUS_MAPPING_CLIENT,
        request->timeout);
    if (status != A_STATUS_OK) return status;
    status = aModbusMappingDataTargetInfo(handle, &request->target,
        request->area, request->word_order, &info);
    protocol = buffer.bytes;
    if (status == A_STATUS_OK) {
        status = aModbusMappingAccessCheck(handle->config, request->unit_id,
            request->area, request->address,
            info.quantity, protocol, sizeof(buffer), write);
    }
    if (status == A_STATUS_OK && write) {
        if (source == NULL) {
            status = aCommBindingRead(&handle->comm,
                &(aCommValueReadRequest_t){
                    .target = request->target, .data = value.bytes,
                    .size = info.size,
                    .timeout = aModbusMappingRemaining(handle)
                });
            source = value.bytes;
        }
        if (status == A_STATUS_OK) {
            status = aModbusMappingValueEncode(&info, request->area,
                request->word_order, source, protocol);
        }
        /* 即时新值也必须通过范围校验，不能依赖本地旧副本已校验。 */
        if (status == A_STATUS_OK) {
            status = aModbusMappingValueDecode(&info, request->area,
                request->word_order, protocol, value.bytes);
        }
    }
    if (status == A_STATUS_OK) {
        const aModbusMapAddress_t target = {request->unit_id, request->area,
            request->address, info.quantity};
        if (write) {
            const aCommWriteAccess_t access = {
                &target, protocol, sizeof(buffer),
                aModbusMappingRemaining(handle), request->result
            };
            status = aCommEndpointWrite(handle->config->protocol, &access);
        } else {
            const aCommReadAccess_t access = {
                &target, protocol, sizeof(buffer),
                aModbusMappingRemaining(handle), request->result
            };
            status = aCommEndpointRead(handle->config->protocol, &access);
        }
        if (status == A_STATUS_OK && !write) {
            status = aModbusMappingValueDecode(&info, request->area,
                request->word_order, protocol, value.bytes);
            if (status == A_STATUS_OK) {
                status = aCommBindingWrite(&handle->comm,
                    &(aCommValueWriteRequest_t){
                        .target = request->target, .data = value.bytes,
                        .size = info.size,
                        .timeout = aModbusMappingRemaining(handle)
                    });
            }
        }
    }
    aAtomicStore(&handle->active, A_FALSE);
    return status;
}
aStatus_t aModbusMappingClientReadSig(aModbusMapping_t *handle,
    const aModbusMappingClientSigRequest_t *request)
{
    return client_sig(handle, request, A_FALSE, NULL);
}
aStatus_t aModbusMappingClientWriteSig(aModbusMapping_t *handle,
    const aModbusMappingClientSigRequest_t *request)
{
    return client_sig(handle, request, A_TRUE, NULL);
}
/* aComm 已检查目标和权限；此处仅将数据字段转换为 Modbus 地址。 */
static aStatus_t requested(void *context, const aCommBindingRequest_t *request)
{
    aModbusMapping_t *handle = context;
    const aModbusClientMap_t *map = (const void *)request->entry;
    aModbusMappingClientSigRequest_t access = {
        .unit_id = handle->client.unit_id, .area = map->area,
        .address = map->address, .target = request->target,
        .word_order = map->word_order,
        .timeout = handle->client.request_timeout
    };
    if (request->target.paramIndex != map->comm.paramIndex) {
        uint16_t offset;
        aStatus_t status = aModbusMapSlice(
            &request->info, request->target.paramIndex, &offset,
            &access.word_order);
        if (status != A_STATUS_OK) return status;
        if (offset > UINT16_MAX - map->address) return A_STATUS_INVALID_PARAM;
        access.address = (uint16_t)(map->address + offset);
    }
    return client_sig(handle, &access, request->write, request->source);
}
void aModbusMappingClientConfigStructInit(aModbusMappingClientConfig_t *config)
{
    if (config != NULL) {
        const aModbusMappingClientConfig_t defaults = {
            .unit_id = 1U, .request_timeout = A_TIMEOUT_MS(1000U)
        };
        *config = defaults;
        aModbusMappingConfigStructInit(&config->binding);
    }
}
static aCommBindingEvents_t events_config(
    const aModbusMappingClientConfig_t *config, aModbusMapping_t *handle)
{
    const aCommBindingEvents_t events = {
        .type = 1U,
        .context = handle, .execute = requested,
        .submit = config->submit, .submit_context = config->submit_context
    };
    return events;
}
static aStatus_t client_config_check(
    const aModbusMappingClientConfig_t *config, aBool_t require_ready)
{
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aModbusMappingConfigCheck(&config->binding);
    if (status != A_STATUS_OK) return status;
    if (require_ready) {
        status = aModbusMapReady(config->binding.protocol);
        if (status != A_STATUS_OK) return status;
    }
    if ((config->maps == NULL) != (config->map_count == 0U) ||
        !aTimeoutIsValid(config->request_timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    for (size_t i = 0U; i < config->map_count; i++) {
        const aModbusClientMap_t *item = &config->maps[i];
        status = aModbusMappingClientMapCheck(
            &config->binding, item, config->unit_id);
        if (status != A_STATUS_OK) return status;
        uint32_t mask = item->comm.subscribe_events;
        /* UPDATED 不自动转成读/写，避免采集发布触发反馈循环。 */
        if (mask != ACOMM_SUBSCRIBE_MARK_ONLY &&
            (mask & ~(ACOMM_REQUEST_READ | ACOMM_REQUEST_WRITE)) != 0U) {
            return A_STATUS_UNSUPPORTED;
        }
        const aCommBindingEvents_t events = events_config(config, NULL);
        status = aCommBindingEntryCheck(&config->binding.comm, &events,
            &item->comm);
        if (status != A_STATUS_OK) return status;
        for (size_t j = 0U; j < i; j++) {
            const aModbusClientMap_t *other = &config->maps[j];
            if ((mask & other->comm.subscribe_events &
                 ACOMM_REQUEST_WRITE) != 0U && other->area == item->area &&
                item->address < (uint32_t)other->address + other->quantity &&
                other->address < (uint32_t)item->address + item->quantity) {
                return A_STATUS_INVALID_PARAM;
            }
        }
    }
    return A_STATUS_OK;
}
aStatus_t aModbusMappingClientConfigCheck(
    const aModbusMappingClientConfig_t *config)
{
    return client_config_check(config, A_TRUE);
}
static size_t client_subscription_count(
    const aModbusMappingClientConfig_t *config)
{
    size_t count = 0U;
    if (config->binding.comm.data->ops->data->subscribe == NULL) return 0U;
    for (size_t i = 0U; i < config->map_count; i++) {
        if (config->maps[i].comm.subscribe_events != 0U) count++;
    }
    return count;
}
static aStatus_t client_init(const aModbusMappingClientConfig_t *config,
    aModbusMapping_t *handle)
{
    aStatus_t status = storage_prepare(handle,
        client_subscription_count(config), config->map_count);
    if (status != A_STATUS_OK) return status;
    handle->client = *config;
    const aCommBindingEvents_t events = events_config(config, handle);
    status = mapping_init(handle, AMODBUS_MAPPING_CLIENT, &events);
    if (status != A_STATUS_OK) return status;
    for (size_t i = 0U; i < config->map_count; i++) {
        const aModbusClientMap_t *item = &config->maps[i];
        status = aCommBindingSubscribe(&handle->comm, &item->comm);
        if (status != A_STATUS_OK) {
            (void)mapping_deinit(handle);
            return status;
        }
    }
    return A_STATUS_OK;
}
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusMappingClientInitStatic(
    const aModbusMappingClientConfig_t *config,
    aModbusMapping_t *handle)
{
    aStatus_t status = aModbusMappingClientConfigCheck(config);
    return status == A_STATUS_OK ? client_init(config, handle) : status;
}
#endif
#if AMODBUS_DYNAMIC_ENABLE
static aStatus_t client_create(const aModbusMappingClientConfig_t *config,
    aModbusMapping_t **handle_out)
{
    aStatus_t status;
    aModbusMapping_t *handle =
        mapping_allocate(&config->binding, client_subscription_count(config),
            config->map_count);
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = client_init(config, handle);
    if (status != A_STATUS_OK) aOSFree(handle);
    else {
        handle->dynamic = A_TRUE;
        *handle_out = handle;
    }
    return status;
}
aStatus_t aModbusMappingClientCreate(const aModbusMappingClientConfig_t *config,
    aModbusMapping_t **handle_out)
{
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    aStatus_t status = aModbusMappingClientConfigCheck(config);
    return status == A_STATUS_OK ? client_create(config, handle_out) : status;
}

#endif

/* -------------------- 注册给 aComm 的数组与批次回调 -------------------- */

_Static_assert(offsetof(aModbusClientMap_t, comm) == 0U,
               "common entry must be first");
_Static_assert(offsetof(aModbusServerMap_t, comm) == 0U,
               "common entry must be first");

static const aCommEntry_t *entry_get(const void *table, size_t index)
{
    const aModbusClientMap_t *entries = table;
    return &entries[index].comm;
}
static aStatus_t entry_check(void *context, const aCommEntry_t *entry)
{
    aModbusMapping_t *handle = context;
    if (handle == NULL || !aTimeoutIsValid(handle->client.request_timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (handle->role != AMODBUS_MAPPING_CLIENT) return A_STATUS_UNSUPPORTED;
    /* 注册表在实例初始化时已检查；独立计划仍可带另一张表。 */
    uintptr_t first = (uintptr_t)handle->client.maps;
    uintptr_t current = (uintptr_t)entry;
    if (handle->client.maps != NULL && current >= first &&
        (current - first) / sizeof(aModbusClientMap_t) <
            handle->client.map_count &&
        (current - first) % sizeof(aModbusClientMap_t) == 0U) {
        return A_STATUS_OK;
    }
    return aModbusMappingClientMapCheck(handle->config,
        (const aModbusClientMap_t *)entry, handle->client.unit_id);
}
static aBool_t entries_merge(void *context, const aCommEntry_t *first_entry,
    const aCommEntry_t *previous_entry, const aCommEntry_t *next_entry)
{
    aModbusMapping_t *handle = context;
    const aModbusClientMap_t *first = (const void *)first_entry;
    const aModbusClientMap_t *previous = (const void *)previous_entry;
    const aModbusClientMap_t *next = (const void *)next_entry;
    if (first->area != next->area ||
        (uint32_t)previous->address + previous->quantity != next->address) {
        return A_FALSE;
    }
    uint32_t quantity = (uint32_t)next->address - first->address;
    aModbusMapArea_t area = aModbusMappingArea(next->area);
    size_t capacity = area.unit_bits == 1U ? ACOMM_VALUE_CAPACITY * 8U :
        ACOMM_VALUE_CAPACITY / (area.unit_bits / 8U);
    if (quantity + next->quantity > capacity || quantity > UINT16_MAX) {
        return A_FALSE;
    }
    const aModbusMapAddress_t accumulated = {handle->client.unit_id,
        first->area, first->address, (uint16_t)quantity};
    const aModbusMapAddress_t candidate = {handle->client.unit_id,
        next->area, next->address, next->quantity};
    return aModbusMapMerge(&accumulated, &candidate);
}
static aModbusMapAddress_t batch_address(aModbusMapping_t *handle,
    const aCommBatch_t *batch)
{
    const aModbusClientMap_t *entries = batch->entries;
    const aModbusClientMap_t *first = &entries[batch->indices[0]];
    const aModbusClientMap_t *last =
        &entries[batch->indices[batch->count - 1U]];
    const aModbusMapAddress_t address = {
        handle->client.unit_id, first->area, first->address,
        (uint16_t)((uint32_t)last->address + last->quantity - first->address)
    };
    return address;
}
static aStatus_t batch_prepare(void *context, const aCommBatch_t *batch,
    aCommPrepared_t *prepared)
{
    aModbusMapping_t *handle = context;
    const aModbusMapAddress_t address = batch_address(handle, batch);
    return aModbusMapPrepare(&address, prepared);
}
static void batch_execute(void *context, const aCommBatch_t *batch,
    const aCommPrepared_t *prepared, aCommStepResult_t *execution,
    aCommEndpointResult_t *response)
{
    aModbusMapping_t *handle = context;
    aModbusMappingDataBuffer_t buffer;
    aStatus_t status = mapping_begin(handle,
        AMODBUS_MAPPING_CLIENT, handle->client.request_timeout);
    execution->advance = A_TRUE;
    if (status == A_STATUS_OK) {
        const aModbusMapAddress_t address = batch_address(handle, batch);
        const aCommReadAccess_t access = {
            .target = &address, .data = &buffer, .size = sizeof(buffer),
            .timeout = aModbusMappingRemaining(handle), .result = response
        };
        aCommEndpoint_t *endpoint = handle->config->protocol;
        if (prepared->size != 0U) {
            status = aModbusMapReadPrepared(endpoint,
                prepared, &access);
        } else {
            status = aCommEndpointRead(endpoint, &access);
        }
        execution->retryable = response->retryable;
        execution->advance = response->started || status != A_STATUS_BUSY;
        if (status == A_STATUS_OK) {
            status = aModbusMappingDataPublish(handle, batch->entries,
                batch->indices, batch->count, &buffer);
        }
        aAtomicStore(&handle->active, A_FALSE);
    } else if (status == A_STATUS_BUSY) execution->advance = A_FALSE;
    execution->status = status;
}
static aStatus_t table_acquire(void *context)
{
    aModbusMapping_t *handle = context;
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (handle->plan_count == SIZE_MAX) return A_STATUS_BUSY;
    handle->plan_count++;
    return A_STATUS_OK;
}
static aStatus_t table_release(void *context)
{
    aModbusMapping_t *handle = context;
    if (aAtomicLoad(&handle->active)) return A_STATUS_BUSY;
    handle->plan_count--;
    return A_STATUS_OK;
}
const aCommTableOps_t aModbusClientTableOps = {
    .get = entry_get, .check = entry_check, .merge = entries_merge,
    .prepare = batch_prepare, .execute = batch_execute,
    .acquire = table_acquire, .release = table_release
};

/* -------------------- 从站映射与同步地址回调 -------------------- */

void aModbusMappingServerMapStructInit(aModbusServerMap_t *map)
{
    if (map != NULL) {
        const aModbusServerMap_t defaults = {
            .area = 0U,
            .quantity = 0U,
            .comm.flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .comm.paramIndex = ACOMM_MAP_SIG_WHOLE
        };
        *map = defaults;
    }
}
void aModbusMappingAddressRangeStructInit(aModbusMappingAddressRange_t *range)
{
    if (range != NULL) {
        const aModbusMappingAddressRange_t defaults = {
            .area = 0U,
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE
        };
        *range = defaults;
    }
}
void aModbusMappingServerConfigStructInit(aModbusMappingServerConfig_t *config)
{
    if (config != NULL) {
        const aModbusMappingServerConfig_t defaults = {0};
        *config = defaults;
        aModbusMappingConfigStructInit(&config->binding);
    }
}
/* 观察回调只传递具体表项，节点、入队和完成机制均由 aComm 管理。 */
static void observe(void *context, const aCommEntry_t *entry,
    const aCommNotifyInput_t *input)
{
    aModbusMapping_t *handle = context;
    handle->server.notify(handle->server.notify_context,
        (const aModbusServerMap_t *)entry, input);
}
aStatus_t aModbusMappingServerConfigCheck(
    const aModbusMappingServerConfig_t *config)
{
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aModbusMappingConfigCheck(&config->binding);
    if (status != A_STATUS_OK) return status;
    if (config->range_count == 0U && config->map_count == 0U) {
        return A_STATUS_INVALID_PARAM;
    }
    return aModbusMappingDataDefinitionsCheck(config);
}
static size_t server_subscription_count(
    const aModbusMappingServerConfig_t *config)
{
    size_t count = 0U;
    if (config->binding.comm.data->ops->data->subscribe == NULL) return 0U;
    for (size_t i = 0U; i < config->map_count; i++) {
        if (config->maps[i].comm.subscribe_events != 0U) count++;
    }
    return count;
}
static aStatus_t server_init(const aModbusMappingServerConfig_t *config,
    aModbusMapping_t *handle)
{
    aStatus_t status = storage_prepare(handle,
        server_subscription_count(config), config->map_count);
    if (status != A_STATUS_OK) return status;
    handle->server = *config;
    const aCommBindingEvents_t events = {
        .type = 1U,
        .context = handle, .observe = config->notify == NULL ? NULL : observe
    };
    status = mapping_init(handle, AMODBUS_MAPPING_SERVER, &events);
    if (status != A_STATUS_OK) return status;
    for (size_t i = 0U; i < config->map_count; i++) {
        const aModbusServerMap_t *item = &config->maps[i];
        status = aCommBindingSubscribe(&handle->comm, &item->comm);
        if (status != A_STATUS_OK) {
            (void)mapping_deinit(handle);
            return status;
        }
    }
    return A_STATUS_OK;
}
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusMappingServerInitStatic(
    const aModbusMappingServerConfig_t *config,
    aModbusMapping_t *handle)
{
    aStatus_t status = aModbusMappingServerConfigCheck(config);
    return status == A_STATUS_OK ? server_init(config, handle) : status;
}
#endif
#if AMODBUS_DYNAMIC_ENABLE
static aStatus_t server_create(const aModbusMappingServerConfig_t *config,
    aModbusMapping_t **handle_out)
{
    aStatus_t status;
    aModbusMapping_t *handle =
        mapping_allocate(&config->binding, server_subscription_count(config),
            config->map_count);
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = server_init(config, handle);
    if (status != A_STATUS_OK) aOSFree(handle);
    else {
        handle->dynamic = A_TRUE;
        *handle_out = handle;
    }
    return status;
}
aStatus_t aModbusMappingServerCreate(const aModbusMappingServerConfig_t *config,
    aModbusMapping_t **handle_out)
{
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    aStatus_t status = aModbusMappingServerConfigCheck(config);
    return status == A_STATUS_OK ? server_create(config, handle_out) : status;
}

#endif
aStatus_t aModbusMappingServerRead(void *context,
    const aModbusAddressReadRequest_t *request)
{
    aModbusMapping_t *handle = context;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = mapping_begin(handle,
        AMODBUS_MAPPING_SERVER, request->timeout);
    if (status != A_STATUS_OK) return status;
    status = aModbusMappingDataRead(handle, request);
    aAtomicStore(&handle->active, A_FALSE);
    return status;
}
aStatus_t aModbusMappingServerWrite(void *context,
    const aModbusAddressWriteRequest_t *request)
{
    aModbusMapping_t *handle = context;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = mapping_begin(handle,
        AMODBUS_MAPPING_SERVER, request->timeout);
    if (status != A_STATUS_OK) return status;
    status = aModbusMappingDataWrite(handle, request);
    aAtomicStore(&handle->active, A_FALSE);
    return status;
}

/* -------------------- aComm 统一注册与协议生命周期 -------------------- */

#if AMODBUS_CLIENT_ENABLE
static aModbusMappingClientConfig_t comm_client_config(
    const aCommProtocol_t *registration)
{
    const aModbusCommConfig_t *config = registration->config;
    const aModbusComm_t *context = registration->context;
    const aModbusMappingClientConfig_t mapping = {
        .binding = {
            .comm = context->binding,
            .protocol = aModbusCommEndpoint(context->protocol)
        },
        .maps = registration->items, .map_count = registration->count,
        .unit_id = config->unit_id, .request_timeout = config->timeout,
        .submit = context->submit, .submit_context = context->submit_context
    };
    return mapping;
}
#endif
#if AMODBUS_SERVER_ENABLE
static aModbusMappingServerConfig_t comm_server_config(
    const aCommProtocol_t *registration)
{
    const aModbusCommConfig_t *config = registration->config;
    const aModbusComm_t *context = registration->context;
    const aModbusMappingServerConfig_t mapping = {
        .binding = {
            .comm = context->binding,
            .protocol = NULL
        },
        .maps = registration->items, .map_count = registration->count,
        .ranges = config->ranges, .range_count = config->range_count,
        .notify = config->notify, .notify_context = config->notify_context
    };
    return mapping;
}
#endif
aStatus_t aModbusCommCheck(const aCommProtocol_t *registration)
{
    if (registration == NULL) return A_STATUS_INVALID_PARAM;
    const aModbusCommConfig_t *config = registration->config;
    const aModbusComm_t *context = registration->context;
    if (config == NULL || context == NULL) return A_STATUS_INVALID_PARAM;
    if (context->protocol != NULL || context->mapping != NULL) {
        return A_STATUS_BUSY;
    }
    if (!aTimeoutIsValid(config->byte_timeout) ||
        !aTimeoutIsValid(config->timeout)) return A_STATUS_INVALID_PARAM;
    if (config->transport_type != AMODBUS_TRANSPORT_RTU &&
        config->transport_type != AMODBUS_TRANSPORT_TCP) {
        return A_STATUS_UNSUPPORTED;
    }
    if (config->role == AMODBUS_ROLE_CLIENT) {
#if AMODBUS_CLIENT_ENABLE
        if (registration->items == NULL || registration->count == 0U ||
            config->ranges != NULL || config->range_count != 0U ||
            config->notify != NULL || config->notify_context != NULL) {
            return A_STATUS_INVALID_PARAM;
        }
        aModbusMappingClientConfig_t mapping = comm_client_config(registration);
        return client_config_check(&mapping, A_FALSE);
#else
        return A_STATUS_UNSUPPORTED;
#endif
    }
    if (config->role == AMODBUS_ROLE_SERVER) {
#if AMODBUS_SERVER_ENABLE
        if ((config->transport_type == AMODBUS_TRANSPORT_RTU &&
             (config->unit_id == 0U || config->unit_id > 247U)) ||
            (config->timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
             config->timeout.milliseconds == 0U)) return A_STATUS_INVALID_PARAM;
        aModbusMappingServerConfig_t mapping = comm_server_config(registration);
        return aModbusMappingServerConfigCheck(&mapping);
#else
        return A_STATUS_UNSUPPORTED;
#endif
    }
    return A_STATUS_INVALID_PARAM;
}

aStatus_t aModbusCommDeInit(void *argument)
{
    aModbusComm_t *context = argument;
    aStatus_t status;
    if (context == NULL) return A_STATUS_INVALID_PARAM;
    if (context->mapping != NULL) {
#if AMODBUS_STATIC_ENABLE
        if (context->mapping == context->mapping_storage) {
            status = aModbusMappingDeInitStatic(context->mapping);
        } else
#endif
        {
#if AMODBUS_DYNAMIC_ENABLE
            status = aModbusMappingDestroy(context->mapping);
#else
            return A_STATUS_INVALID_PARAM;
#endif
        }
        if (status != A_STATUS_OK) return status;
        context->mapping = NULL;
    }
    if (context->protocol != NULL) {
#if AMODBUS_STATIC_ENABLE
        if (context->protocol == context->protocol_storage) {
            status = aModbusDeInitStatic(context->protocol);
        } else
#endif
        {
#if AMODBUS_DYNAMIC_ENABLE
            status = aModbusDestroy(context->protocol);
#else
            return A_STATUS_INVALID_PARAM;
#endif
        }
        if (status != A_STATUS_OK) return status;
        context->protocol = NULL;
    }
    return A_STATUS_OK;
}

aStatus_t aModbusCommInit(const aCommProtocol_t *registration,
    aCommPlanConfig_t *table)
{
    if (table == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aModbusCommCheck(registration);
    if (status != A_STATUS_OK) return status;
    const aModbusCommConfig_t *config = registration->config;
    aModbusComm_t *context = registration->context;
    /* 产品参数只描述协议；实际收发能力由应用提供的通道注入。 */
    aModbusConfig_t transport = {
        .transport_type = config->transport_type,
        .byte_timeout = config->byte_timeout
    };
    status = aModbusBindChannel(context->channel, &transport.transport);
    if (status != A_STATUS_OK) return status;
    if (config->role == AMODBUS_ROLE_CLIENT) {
#if AMODBUS_CLIENT_ENABLE
        const aModbusClientConfig_t client = {.modbus = transport};
#if AMODBUS_STATIC_ENABLE
        if (context->protocol_storage != NULL) {
            status = aModbusClientInitStatic(&client,
                context->protocol_storage);
            if (status == A_STATUS_OK) {
                context->protocol = context->protocol_storage;
            }
        } else
#endif
        {
#if AMODBUS_DYNAMIC_ENABLE
            status = aModbusClientCreate(&client, &context->protocol);
#else
            status = A_STATUS_INVALID_PARAM;
#endif
        }
        if (status != A_STATUS_OK) goto cleanup;
        aModbusMappingClientConfig_t mapping = comm_client_config(registration);
#if AMODBUS_STATIC_ENABLE
        if (context->mapping_storage != NULL) {
            status = client_init(&mapping,
                context->mapping_storage);
            if (status == A_STATUS_OK) {
                context->mapping = context->mapping_storage;
            }
        } else
#endif
        {
#if AMODBUS_DYNAMIC_ENABLE
            status = client_create(&mapping, &context->mapping);
#else
            status = A_STATUS_INVALID_PARAM;
#endif
        }
        if (status != A_STATUS_OK) goto cleanup;
        *table = (aCommPlanConfig_t){
            .items = registration->items, .count = registration->count,
            .ops = &aModbusClientTableOps, .context = context->mapping
        };
#endif
    } else {
#if AMODBUS_SERVER_ENABLE
        aModbusMappingServerConfig_t mapping = comm_server_config(registration);
#if AMODBUS_STATIC_ENABLE
        if (context->mapping_storage != NULL) {
            status = server_init(&mapping,
                context->mapping_storage);
            if (status == A_STATUS_OK) {
                context->mapping = context->mapping_storage;
            }
        } else
#endif
        {
#if AMODBUS_DYNAMIC_ENABLE
            status = server_create(&mapping, &context->mapping);
#else
            status = A_STATUS_INVALID_PARAM;
#endif
        }
        if (status != A_STATUS_OK) goto cleanup;
        const aModbusServerConfig_t server = {
            .modbus = transport, .unit_id = config->unit_id,
            .read = aModbusMappingServerRead,
            .write = aModbusMappingServerWrite, .context = context->mapping
        };
#if AMODBUS_STATIC_ENABLE
        if (context->protocol_storage != NULL) {
            status = aModbusServerInitStatic(&server,
                context->protocol_storage);
            if (status == A_STATUS_OK) {
                context->protocol = context->protocol_storage;
            }
        } else
#endif
        {
#if AMODBUS_DYNAMIC_ENABLE
            status = aModbusServerCreate(&server, &context->protocol);
#else
            status = A_STATUS_INVALID_PARAM;
#endif
        }
        if (status != A_STATUS_OK) goto cleanup;
        context->process = (aModbusServerProcessRequest_t){
            .timeout = config->timeout
        };
        *table = (aCommPlanConfig_t){0};
#endif
    }
    return A_STATUS_OK;
cleanup: {
        aStatus_t cleanup = aModbusCommDeInit(context);
        return cleanup == A_STATUS_OK ? status : cleanup;
    }
}
void aModbusCommProcess(void *argument, size_t step,
    aCommStepResult_t *result)
{
    (void)step;
    if (result == NULL) return;
    *result = (aCommStepResult_t){
        .status = A_STATUS_INVALID_PARAM, .advance = A_TRUE
    };
    if (argument == NULL) return;
#if AMODBUS_SERVER_ENABLE
    aModbusComm_t *context = argument;
    result->status = aModbusServerProcess(context->protocol, &context->process);
#else
    (void)argument;
    result->status = A_STATUS_UNSUPPORTED;
#endif
}
