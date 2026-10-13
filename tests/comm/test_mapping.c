/* 只链接 aComm/aLib/aOS：以字节寻址端点验证映射并未写死寄存器协议。 */
#include "aComm_mapping_instance.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void testAdvanceTime(uint32_t milliseconds);
void aOSDelayMs(uint32_t milliseconds) { testAdvanceTime(milliseconds); }

typedef struct {
    aCommEndpoint_t base;
    uint8_t values[2][3];
} data_t;
typedef struct {
    aCommEndpoint_t base;
    uint8_t bytes[32];
    unsigned reads, prepared_reads;
    aBool_t fail_prepare;
} wire_t;

static aStatus_t info(aCommEndpoint_t *base, const aCommDataQuery_t *query,
    aCommDataInfo_t *result)
{
    (void)base;
    if (query->deviceID != 7U || query->sigIndex >= 2U) {
        return A_STATUS_NOT_FOUND;
    }
    *result = (aCommDataInfo_t){.type = ALIB_DATA_RAW, .size = 3U};
    return A_STATUS_OK;
}
static aStatus_t data_read(aCommEndpoint_t *base,
    const aCommReadAccess_t *request)
{
    const aCommTarget_t *target = request->target;
    assert(target->paramIndex == ACOMM_DATA_WHOLE);
    assert(request->size == 3U);
    memcpy(request->data, ((data_t *)base)->values[target->sigIndex], 3U);
    return A_STATUS_OK;
}
static aStatus_t data_write(aCommEndpoint_t *base,
    const aCommWriteAccess_t *request)
{
    const aCommTarget_t *target = request->target;
    assert(target->paramIndex == ACOMM_DATA_WHOLE);
    assert(request->size == 3U);
    memcpy(((data_t *)base)->values[target->sigIndex], request->data, 3U);
    return A_STATUS_OK;
}
static aStatus_t update(aCommEndpoint_t *base,
    const aCommDataUpdate_t *request)
{
    data_t *data = (data_t *)base;
    assert(request->capacity >= 3U);
    memcpy(request->buffer, data->values[request->target.sigIndex], 3U);
    aStatus_t status = request->edit(request->context, request->buffer, 3U);
    if (status == A_STATUS_OK) {
        memcpy(data->values[request->target.sigIndex], request->buffer, 3U);
    }
    return status;
}
static aStatus_t ready(aCommEndpoint_t *base)
{
    (void)base;
    return A_STATUS_OK;
}
static aStatus_t area(aCommEndpoint_t *base, uint16_t id,
    aCommAreaInfo_t *result)
{
    (void)base;
    if (id != 9U) return A_STATUS_INVALID_PARAM;
    *result = (aCommAreaInfo_t){.unit_bits = 8U, .read_limit = 6U,
        .write_limit = 6U, .address_limit = 32U, .writable = A_TRUE};
    return A_STATUS_OK;
}
static aStatus_t check(aCommEndpoint_t *base, const aCommAddress_t *address,
    const void *data, size_t size, aBool_t write)
{
    (void)base;
    (void)write;
    return address->area == 9U && address->quantity > 0U &&
        address->quantity <= 6U && address->address + address->quantity <=
        32U && data != NULL && size >= address->quantity ?
        A_STATUS_OK : A_STATUS_INVALID_PARAM;
}
static aStatus_t describe(const aCommDataInfo_t *data,
    const aCommTarget_t *target, uint16_t region, uint8_t order,
    aCommValueInfo_t *result)
{
    if (target->paramIndex != ACOMM_DATA_WHOLE || region != 9U || order != 0U) {
        return A_STATUS_UNSUPPORTED;
    }
    *result = (aCommValueInfo_t){.type = data->type, .size = data->size,
        .quantity = (uint16_t)data->size};
    return A_STATUS_OK;
}
static aStatus_t layout(const aCommValueInfo_t *value)
{
    return value->type == ALIB_DATA_RAW ? A_STATUS_OK : A_STATUS_UNSUPPORTED;
}
static aStatus_t copy(const aCommValueInfo_t *value, uint16_t region,
    uint8_t order, size_t first, size_t count, void *local, void *remote,
    aBool_t write)
{
    assert(region == 9U && order == 0U);
    assert(first + count <= value->size);
    if (write) memcpy((uint8_t *)local + first, remote, count);
    else memcpy(remote, (uint8_t *)local + first, count);
    return A_STATUS_OK;
}
static aStatus_t slice(const aCommDataInfo_t *data, size_t field,
    uint16_t *offset, uint8_t *order)
{
    (void)data; (void)field; (void)offset; (void)order;
    return A_STATUS_UNSUPPORTED;
}
static aBool_t merge(aCommEndpoint_t *base, const aCommAddress_t *first,
    const aCommAddress_t *next)
{
    (void)base;
    return first->area == next->area && first->unit_id == next->unit_id &&
        first->address + first->quantity == next->address &&
        first->quantity + next->quantity <= 6U;
}
static aStatus_t wire_read(aCommEndpoint_t *base,
    const aCommReadAccess_t *request)
{
    wire_t *wire = (wire_t *)base;
    const aCommAddress_t *target = request->target;
    assert(check(base, target, request->data, request->size, A_FALSE) ==
        A_STATUS_OK);
    wire->reads++;
    memcpy(request->data, wire->bytes + target->address, target->quantity);
    if (request->result != NULL) request->result->started = A_TRUE;
    return A_STATUS_OK;
}
static aStatus_t wire_write(aCommEndpoint_t *base,
    const aCommWriteAccess_t *request)
{
    wire_t *wire = (wire_t *)base;
    const aCommAddress_t *target = request->target;
    assert(check(base, target, request->data, request->size, A_TRUE) ==
        A_STATUS_OK);
    memcpy(wire->bytes + target->address, request->data, target->quantity);
    return A_STATUS_OK;
}
static aStatus_t prepare(aCommEndpoint_t *base, const aCommAddress_t *address,
    aCommPrepared_t *result)
{
    if (((wire_t *)base)->fail_prepare) return A_STATUS_ERROR;
    result->data[0] = 0x42U;
    result->size = 1U;
    result->address = *address;
    return A_STATUS_OK;
}
static aStatus_t prepared_read(aCommEndpoint_t *base,
    const aCommPrepared_t *prepared, const aCommReadAccess_t *request)
{
    assert(prepared->size == 1U && prepared->data[0] == 0x42U);
    ((wire_t *)base)->prepared_reads++;
    return wire_read(base, request);
}
static const aCommDataOps_t data_ops = {.info = info, .update = update};
static const aCommEndpointOps_t local_ops = {
    .read = data_read, .write = data_write, .data = &data_ops
};
static const aCommAddressOps_t address_ops = {
    .ready = ready, .area = area, .check = check, .describe = describe,
    .layout = layout, .copy = copy, .slice = slice, .merge = merge,
    .prepare = prepare, .read_prepared = prepared_read
};
static const aCommEndpointOps_t wire_ops = {
    .read = wire_read, .write = wire_write, .address = &address_ops
};

static void exercise(aBool_t preencode)
{
    data_t local = {.base.ops = &local_ops};
    aCommAddressOps_t rules = address_ops;
    aCommEndpointOps_t operations = wire_ops;
    if (!preencode) { rules.prepare = NULL; rules.read_prepared = NULL; }
    operations.address = &rules;
    wire_t wire = {.base.ops = &operations};
    const aCommMappingClientMap_t maps[] = {
        {.area = 9U, .address = 2U, .quantity = 3U,
         .target = {7U, 0U, ACOMM_DATA_WHOLE, 0U, 0U},
         .access = ACOMM_MAP_ACCESS_READ, .poll_time_ms = 10U},
        {.area = 9U, .address = 5U, .quantity = 3U,
         .target = {7U, 1U, ACOMM_DATA_WHOLE, 0U, 0U},
         .access = ACOMM_MAP_ACCESS_READ, .poll_time_ms = 10U}
    };
    const aCommMappingClientConfig_t config = {
        .binding = {.data = &local.base, .protocol = &wire.base},
        .maps = maps, .map_count = 2U, .unit_id = 1U,
        .request_timeout = A_TIMEOUT_MS(10U)
    };
    const aCommPlanConfig_t plan_config = {
        .items = maps, .count = 2U, .unit_id = 1U,
        .timeout = A_TIMEOUT_MS(10U)
    };
    aCommMapping_t *mapping;
    aCommPlan_t *plan;
#if ACOMM_STATIC_ENABLE
    aCommMapping_t storage = {0};
    size_t indices[2];
    aCommPlanFrame_t frames[2];
    aCommPlan_t plan_storage = {.frames = frames, .capacity = 2U,
        .indices = indices, .index_capacity = 2U};
    mapping = &storage;
    plan = &plan_storage;
    assert(aCommMappingClientInitStatic(&config, mapping) == A_STATUS_OK);
#else
    assert(aCommMappingClientCreate(&config, &mapping) == A_STATUS_OK);
#endif
    if (preencode) {
        wire.fail_prepare = A_TRUE;
#if ACOMM_STATIC_ENABLE
        assert(aCommPlanInitStatic(mapping, &plan_config, plan) == A_STATUS_ERROR);
        assert(!plan->ready);
#else
        assert(aCommPlanCreate(mapping, &plan_config, &plan) == A_STATUS_ERROR);
        assert(plan == NULL);
#endif
        assert(mapping->plan_count == 0U);
        wire.fail_prepare = A_FALSE;
    }
#if ACOMM_STATIC_ENABLE
    assert(aCommPlanInitStatic(mapping, &plan_config, plan) == A_STATUS_OK);
#else
    assert(aCommPlanCreate(mapping, &plan_config, &plan) == A_STATUS_OK);
#endif
    assert(aCommPlanFrameCount(plan) == 1U);
    for (unsigned i = 0U; i < 6U; i++) wire.bytes[2U + i] = (uint8_t)(i + 1U);
    aCommPlanRequest_t request = {0};
    assert(aCommPlanExecute(plan, &request) == A_STATUS_OK);
    assert(wire.reads == 1U && wire.prepared_reads == (unsigned)preencode);
    assert(memcmp(local.values, wire.bytes + 2U, 6U) == 0);
    aCommMappingClientSigRequest_t write = {
        .unit_id = 1U, .area = 9U, .address = 20U,
        .target = {7U, 1U, ACOMM_DATA_WHOLE}, .timeout = A_TIMEOUT_MS(10U)
    };
    assert(aCommMappingClientWriteSig(mapping, &write) == A_STATUS_OK);
    assert(memcmp(wire.bytes + 20U, local.values[1], 3U) == 0);
#if ACOMM_STATIC_ENABLE
    assert(aCommPlanDeInitStatic(plan) == A_STATUS_OK);
    assert(aCommMappingDeInitStatic(mapping) == A_STATUS_OK);
#else
    assert(aCommPlanDestroy(plan) == A_STATUS_OK);
    assert(aCommMappingDestroy(mapping) == A_STATUS_OK);
#endif
    const aCommMappingServerMap_t server_maps[] = {
        {.area = 9U, .address = 2U, .quantity = 3U,
         .flags = ACOMM_MAP_ACCESS_READ | ACOMM_MAP_ACCESS_WRITE,
         .target = {7U, 0U, ACOMM_DATA_WHOLE, 0U, 0U}}
    };
    const aCommMappingServerConfig_t server = {
        .binding = config.binding, .maps = server_maps, .map_count = 1U
    };
#if ACOMM_STATIC_ENABLE
    assert(aCommMappingServerInitStatic(&server, mapping) == A_STATUS_OK);
#else
    assert(aCommMappingServerCreate(&server, &mapping) == A_STATUS_OK);
#endif
    uint8_t bytes[3] = {0};
    aCommAddressReadRequest_t read = {
        .area = 9U, .address = 3U, .quantity = 2U,
        .data = bytes, .size = sizeof(bytes), .timeout = A_TIMEOUT_MS(10U)
    };
    assert(aCommMappingServerRead(mapping, &read) == A_STATUS_OK);
    assert(bytes[0] == 2U && bytes[1] == 3U);
    bytes[0] = 7U;
    aCommAddressWriteRequest_t set = {
        .area = 9U, .address = 2U, .quantity = 3U,
        .data = bytes, .size = sizeof(bytes), .timeout = A_TIMEOUT_MS(10U)
    };
    assert(aCommMappingServerWrite(mapping, &set) == A_STATUS_OK);
    assert(memcmp(bytes, local.values[0], 3U) == 0);
#if ACOMM_STATIC_ENABLE
    assert(aCommMappingDeInitStatic(mapping) == A_STATUS_OK);
#else
    assert(aCommMappingDestroy(mapping) == A_STATUS_OK);
#endif
}
int main(void)
{
    exercise(A_FALSE);
    exercise(A_TRUE);
    puts("Generic endpoints: byte mapping/merge/prepare/lifecycle passed");
    return 0;
}
