#include "aModbus.h"
#include "aModbus_endpoint.h"
#include "aBus_endpoint.h"
#include "aModbus_mapping_instance.h"
#include "aModbus_instance.h"
#include "aComm_instance.h"
#include "aBus_instance.h"
#include "aOS.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void testAdvanceTime(uint32_t milliseconds);
void testSetTime(uint32_t milliseconds);
void testFailAllocation(aBool_t fail);
unsigned testAllocations(void);
/* 夹具把旧测试的设备参数应用到协议上下文，计划本身不含协议字段。 */
typedef struct {
    const aModbusClientMap_t *items;
    size_t count;
    uint8_t unit_id;
    aTimeout_t timeout;
} fixturePlanConfig_t;
static inline void fixturePlanConfigInit(fixturePlanConfig_t *config)
{
    *config = (fixturePlanConfig_t){.unit_id = 1U,
                                   .timeout = A_TIMEOUT_MS(1000U)};
}
static inline aCommPlanConfig_t fixturePlanConfig(
    aModbusMapping_t *handle, const fixturePlanConfig_t *config)
{
    handle->client.unit_id = config->unit_id;
    handle->client.request_timeout = config->timeout;
    const aCommPlanConfig_t table = {
        config->items, config->count, &aModbusClientTableOps, handle
    };
    return table;
}
#if ACOMM_STATIC_ENABLE
static inline aStatus_t fixturePlanInitStatic(aModbusMapping_t *handle,
    const fixturePlanConfig_t *config, aCommPlan_t *plan)
{
    const aCommPlanConfig_t table = fixturePlanConfig(handle, config);
    return aCommPlanInitStatic(&table, plan);
}
#endif
#if ACOMM_DYNAMIC_ENABLE
static inline aStatus_t fixturePlanCreate(aModbusMapping_t *handle,
    const fixturePlanConfig_t *config, aCommPlan_t **plan)
{
    const aCommPlanConfig_t table = fixturePlanConfig(handle, config);
    return aCommPlanCreate(&table, plan);
}
#endif
static aModbusHandle_t *fixtureProtocol(const aModbusMapping_t *mapping);

void aOSDelayMs(uint32_t milliseconds) { testAdvanceTime(milliseconds); }

enum { WORD = 10, COUNTER, SIGNED, MOTOR, RAW, LOCAL, SIG_COUNT };
typedef struct { uint32_t speed; int32_t temperature; } motor_t;
static uint8_t flags[10];
static uint16_t word;
static uint32_t counter;
static int32_t signed_value;
static motor_t motor;
static uint8_t raw[5];
static uint32_t local_value;
static uint32_t other_counter;
static aBusHandle_t bus;
static aBusSigState_t states[SIG_COUNT + 1];
static const uint16_t word_default = 123U;
static const uint32_t counter_default = 0x12345678U;
static const int32_t signed_default = -42;
static const motor_t motor_default = {3000U, 25};
static const uint8_t raw_default[] = {0x11, 0x22, 0x33, 0x44, 0x55};
static const aBusRange_t word_range = {.min.u16 = 0, .max.u16 = 1000};
static const aBusRange_t speed_range = {.min.u32 = 0, .max.u32 = 6000};
static const aBusParam_t params[] = {
    {.offset = offsetof(motor_t, speed), .size = sizeof(uint32_t),
     .type = ALIB_DATA_U32, .range = &speed_range},
    {.offset = offsetof(motor_t, temperature), .size = sizeof(int32_t),
     .type = ALIB_DATA_S32}
};
#define FLAG_DEF(key) {.sigKey = key, .type = ALIB_DATA_U8, .size = 1U}
static const aBusSig_t sigs[SIG_COUNT] = {
    FLAG_DEF(0), FLAG_DEF(1), FLAG_DEF(2), FLAG_DEF(3), FLAG_DEF(4),
    FLAG_DEF(5), FLAG_DEF(6), FLAG_DEF(7), FLAG_DEF(8), FLAG_DEF(9),
    [WORD] = {.sigKey = 10, .type = ALIB_DATA_U16, .size = sizeof(word),
        .default_data = &word_default, .range = &word_range,
        .flags = ABUS_SIG_FLAG_LOCK},
    [COUNTER] = {.sigKey = 11, .type = ALIB_DATA_U32,
        .size = sizeof(counter), .default_data = &counter_default,
        .flags = ABUS_SIG_FLAG_LOCK},
    [SIGNED] = {.sigKey = 12, .type = ALIB_DATA_S32,
        .size = sizeof(signed_value), .default_data = &signed_default},
    [MOTOR] = {.sigKey = 13, .type = ALIB_DATA_STRUCT,
        .size = sizeof(motor), .default_data = &motor_default,
        .params = params, .param_count = 2, .flags = ABUS_SIG_FLAG_LOCK},
    [RAW] = {.sigKey = 14, .type = ALIB_DATA_RAW, .size = sizeof(raw),
        .default_data = raw_default},
    [LOCAL] = {.sigKey = 15, .type = ALIB_DATA_U32,
        .size = sizeof(local_value), .flags = ABUS_SIG_FLAG_LOCK}
};
static const aBusTable_t tables[] = {
    {.deviceID = 1, .sigs = sigs, .sig_count = SIG_COUNT},
    {.deviceID = 2, .sigs = &sigs[COUNTER], .sig_count = 1}
};
ABUS_RAM_BIND_EXPORT(f0, 7, 1, 0, flags[0]);
ABUS_RAM_BIND_EXPORT(f1, 7, 1, 1, flags[1]);
ABUS_RAM_BIND_EXPORT(f2, 7, 1, 2, flags[2]);
ABUS_RAM_BIND_EXPORT(f3, 7, 1, 3, flags[3]);
ABUS_RAM_BIND_EXPORT(f4, 7, 1, 4, flags[4]);
ABUS_RAM_BIND_EXPORT(f5, 7, 1, 5, flags[5]);
ABUS_RAM_BIND_EXPORT(f6, 7, 1, 6, flags[6]);
ABUS_RAM_BIND_EXPORT(f7, 7, 1, 7, flags[7]);
ABUS_RAM_BIND_EXPORT(f8, 7, 1, 8, flags[8]);
ABUS_RAM_BIND_EXPORT(f9, 7, 1, 9, flags[9]);
ABUS_RAM_BIND_EXPORT(w, 7, 1, WORD, word);
ABUS_RAM_BIND_EXPORT(c, 7, 1, COUNTER, counter);
ABUS_RAM_BIND_EXPORT(s, 7, 1, SIGNED, signed_value);
ABUS_RAM_BIND_EXPORT(m, 7, 1, MOTOR, motor);
ABUS_RAM_BIND_EXPORT(r, 7, 1, RAW, raw);
ABUS_RAM_BIND_EXPORT(l, 7, 1, LOCAL, local_value);
ABUS_RAM_BIND_EXPORT(o, 7, 2, 0, other_counter);

static unsigned address_reads;
static unsigned address_writes;
static unsigned sig_reads;
static unsigned sig_writes;
#if AMODBUS_SERVER_ENABLE
static aModbusMapping_t *callback_instance;
#endif
static aBool_t reject_sig_write;

static aStatus_t address_read(void *context,
    const aModbusAddressReadRequest_t *request)
{
    uint8_t *bits = request->data;
    assert(context == &bus);
    address_reads++;
    memset(bits, 0, request->size);
    for (size_t i = 0U; i < request->quantity; i++) {
        uint8_t value;
        const aBusGetIndexRequest_t read = {
            .deviceID = 1, .sigIndex = request->address + i,
            .dst = &value, .size = sizeof(value),
            .timeout = request->timeout
        };
        assert(aBusGetByIndex(&bus, &read) == A_STATUS_OK);
        nmbs_bitfield_write(bits, i, value);
    }
    return A_STATUS_OK;
}

static aStatus_t address_write(void *context,
    const aModbusAddressWriteRequest_t *request)
{
    const uint8_t *bits = request->data;
    assert(context == &bus);
    address_writes++;
    for (size_t i = 0U; i < request->quantity; i++) {
        uint8_t value = nmbs_bitfield_read(bits, i) ? 1U : 0U;
        const aBusSetIndexRequest_t write = {
            .deviceID = 1, .sigIndex = request->address + i,
            .src = &value, .size = sizeof(value),
            .timeout = request->timeout
        };
        assert(aBusSetByIndex(&bus, &write) == A_STATUS_OK);
    }
    return A_STATUS_OK;
}

#define READ_WRITE (ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE)
#define MAP(addr, count, index) \
    {.area = AMODBUS_AREA_HOLDING_REGISTERS, \
     .address = addr, .quantity = count, .comm.flags = READ_WRITE, \
     .comm.deviceID = 1, \
                   .comm.sigIndex = index, \
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE}
#define COIL_MAP(addr) \
    {.area = AMODBUS_AREA_COILS, .address = addr, \
     .quantity = 1U, .comm.flags = READ_WRITE, \
     .comm.deviceID = 1, \
                   .comm.sigIndex = addr, \
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE}
static const aModbusServerMap_t maps[] = {
    COIL_MAP(0), COIL_MAP(1), COIL_MAP(2), COIL_MAP(3), COIL_MAP(4),
    MAP(0, 1U, WORD), MAP(1, 2U, COUNTER), MAP(3, 2U, SIGNED),
    {.area = AMODBUS_AREA_HOLDING_REGISTERS,
     .address = 5, .quantity = 2U,
     .comm.flags = READ_WRITE, .comm.deviceID = 1,
                   .comm.sigIndex = MOTOR,
                   .comm.paramIndex = 0},
    MAP(7, 3U, RAW),
    {.area = AMODBUS_AREA_HOLDING_REGISTERS,
     .address = 10, .quantity = 2U, .comm.flags = READ_WRITE,
     .comm.deviceID = 2,
                   .comm.sigIndex = 0,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE},
    {.area = AMODBUS_AREA_INPUT_REGISTERS,
     .address = 0, .quantity = 2U, .comm.flags = ACOMM_MAP_FLAG_READ,
     .comm.deviceID = 1,
                   .comm.sigIndex = COUNTER,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE,
     .word_order = AMODBUS_WORD_LOW_FIRST}
};
#undef COIL_MAP
static const aModbusMappingAddressRange_t ranges[] = {
    {.area = AMODBUS_AREA_COILS, .address = 5, .quantity = 5,
     .flags = READ_WRITE, .read = address_read, .write = address_write,
     .context = &bus},
    {.area = AMODBUS_AREA_DISCRETE_INPUTS, .quantity = 10,
     .flags = ACOMM_MAP_FLAG_READ, .read = address_read, .context = &bus}
};

static aStatus_t sig_read(void *context,
    const aCommValueReadRequest_t *request)
{
    assert(context == &bus);
    sig_reads++;
    if (request->target.paramIndex == ACOMM_MAP_SIG_WHOLE) {
        const aBusGetIndexRequest_t read = {
            .deviceID = request->target.deviceID,
            .sigIndex = request->target.sigIndex,
            .dst = request->data, .size = request->size,
            .timeout = request->timeout
        };
        return aBusGetByIndex(&bus, &read);
    } else {
        const aBusGetParamRequest_t read = {
            .deviceID = request->target.deviceID,
            .sigIndex = request->target.sigIndex,
            .paramIndex = request->target.paramIndex,
            .dst = request->data, .size = request->size,
            .timeout = request->timeout
        };
        return aBusGetParam(&bus, &read);
    }
}

static aStatus_t sig_write(void *context,
    const aCommValueWriteRequest_t *request)
{
    assert(context == &bus);
    sig_writes++;
#if AMODBUS_SERVER_ENABLE
    if (callback_instance != NULL) {
        aModbusServerProcessRequest_t nested;
        aModbusServerProcessRequestStructInit(&nested);
        assert(aModbusServerProcess(fixtureProtocol(callback_instance), &nested) ==
               A_STATUS_BUSY);
    }
#endif
    if (reject_sig_write) return A_STATUS_BUSY;
    if (request->target.paramIndex == ACOMM_MAP_SIG_WHOLE) {
        const aBusSetIndexRequest_t write = {
            .deviceID = request->target.deviceID,
            .sigIndex = request->target.sigIndex,
            .src = request->data, .size = request->size,
            .timeout = request->timeout
        };
        return aBusSetByIndex(&bus, &write);
    } else {
        const aBusSetParamRequest_t write = {
            .deviceID = request->target.deviceID,
            .sigIndex = request->target.sigIndex,
            .paramIndex = request->target.paramIndex,
            .src = request->data, .size = request->size,
            .timeout = request->timeout
        };
        return aBusSetParam(&bus, &write);
    }
}

typedef struct link link_t;
typedef struct { link_t *link; aBool_t server; } endpoint_t;
struct link {
    uint8_t request[512];
    size_t request_size;
    size_t request_pos;
    uint8_t response[512];
    size_t response_size;
    size_t response_pos;
    aModbusMapping_t *server;
    aModbusTransportType_t type;
    aBool_t processed;
    aBool_t corrupt;
    unsigned invalid_response;
    aBool_t short_tcp;
    uint32_t delay;
    unsigned completed;
    unsigned prepared;
    aBool_t buffered, fail_flush, fail_write, eof;
    aBool_t zero_write, eof_server;
    aBool_t prepare_busy;
    size_t eof_after;
    size_t pending[2];
    unsigned flushes[2], writes[2];
};

static aBool_t port_wait(link_t *link, aTimeout_t timeout)
{
    if (timeout.type != A_TIMEOUT_TYPE_FOREVER &&
        timeout.milliseconds < link->delay) {
        testAdvanceTime(timeout.milliseconds);
        return A_FALSE;
    }
    testAdvanceTime(link->delay);
    return A_TRUE;
}

#if AMODBUS_SERVER_ENABLE
static void serve(link_t *link)
{
    aModbusServerProcessRequest_t request;
    aModbusServerProcessRequestStructInit(&request);
    request.timeout = A_TIMEOUT_MS(1000U);
    link->processed = A_TRUE;
    assert(aModbusServerProcess(fixtureProtocol(link->server), &request) == A_STATUS_OK);
    if (link->corrupt && link->response_size != 0U) {
        link->response[link->response_size - 1U] ^= 1U;
    }
    if (link->type == AMODBUS_TRANSPORT_RTU && link->response_size >= 5U) {
        switch (link->invalid_response) {
        case 1U: link->response[0]++; break;
        case 2U: link->response[1] = 4U; break;
        case 3U: link->response[2]--; break;
        case 4U: link->response_size--; break;
        default: break;
        }
    }
    if (link->short_tcp && link->response_size >= 8U) {
        link->response[4] = 0U;
        link->response[5] = 2U;
        link->response_size = 8U;
    }
}
#endif

static aSSize_t port_read(void *context, void *data, size_t size,
                          aTimeout_t timeout)
{
    endpoint_t *endpoint = context;
    link_t *link = endpoint->link;
    uint8_t *buffer;
    size_t *position;
    size_t available;
    if ((!endpoint->server && link->eof) ||
        (endpoint->server && link->eof_server)) {
        (void)aOSFailWithStatus(A_STATUS_TIMEOUT);
        return 0; /* EOF 不应读取之前残留的 errno。 */
    }
    if (link->buffered) assert(link->pending[0] == 0U);
    if (!port_wait(link, timeout)) return aOSFailWithStatus(A_STATUS_TIMEOUT);
#if AMODBUS_SERVER_ENABLE
    if (!endpoint->server && !link->processed && link->request_size != 0U) {
        serve(link);
    }
#endif
    buffer = endpoint->server ? link->request : link->response;
    position = endpoint->server ? &link->request_pos : &link->response_pos;
    if (!endpoint->server && link->eof_after != 0U &&
        *position >= link->eof_after) return 0;
    available = (endpoint->server ? link->request_size :
                 link->response_size) - *position;
    if (available == 0U) return aOSFailWithStatus(A_STATUS_BUSY);
    if (size > available) size = available;
    if (size > 2U) size = 2U;
    memcpy(data, buffer + *position, size);
    *position += size;
    return (aSSize_t)size;
}

static aSSize_t port_write(void *context, const void *data, size_t size,
                           aTimeout_t timeout)
{
    endpoint_t *endpoint = context;
    link_t *link = endpoint->link;
    uint8_t *buffer = endpoint->server ? link->response : link->request;
    size_t *used = endpoint->server ? &link->response_size :
                                     &link->request_size;
    if (!endpoint->server && link->zero_write) return 0;
    if (!endpoint->server && link->fail_write && link->writes[0] != 0U) {
        return aOSFailWithStatus(A_STATUS_ERROR);
    }
    if (!port_wait(link, timeout)) return aOSFailWithStatus(A_STATUS_TIMEOUT);
    if (size > 3U) size = 3U;
    assert(*used + size <= sizeof(link->request));
    memcpy(buffer + *used, data, size);
    *used += size;
    link->writes[endpoint->server ? 1 : 0]++;
    if (link->buffered) link->pending[endpoint->server ? 1 : 0] += size;
    return (aSSize_t)size;
}

static aStatus_t port_discard(void *context, aTimeout_t timeout)
{
    endpoint_t *endpoint = context;
    link_t *link = endpoint->link;
    (void)timeout;
    if (endpoint->server) link->request_pos = link->request_size;
    else {
        link->request_pos = 0U;
        link->request_size = 0U;
        link->response_pos = 0U;
        link->response_size = 0U;
        link->processed = A_FALSE;
    }
    return A_STATUS_OK;
}

static aStatus_t port_prepare(void *context, aTimeout_t timeout)
{
    endpoint_t *endpoint = context;
    (void)timeout;
    if (endpoint->link->prepare_busy) return A_STATUS_BUSY;
    endpoint->link->prepared++;
    return A_STATUS_OK;
}

static aStatus_t port_complete(void *context, aTimeout_t timeout)
{
    endpoint_t *endpoint = context;
    (void)timeout;
    if (endpoint->link->buffered) {
        assert(endpoint->link->pending[endpoint->server ? 1 : 0] == 0U);
    }
    endpoint->link->completed++;
#if AMODBUS_SERVER_ENABLE
    if (!endpoint->server &&
        endpoint->link->type == AMODBUS_TRANSPORT_RTU &&
        endpoint->link->request[0] == 0U) serve(endpoint->link);
#endif
    return A_STATUS_OK;
}

#if AMODBUS_CLIENT_ENABLE && AMODBUS_SERVER_ENABLE
static aStatus_t stream_flush(void *context, aTimeout_t timeout)
{
    endpoint_t *endpoint = context;
    size_t index = endpoint->server ? 1U : 0U;
    link_t *link = endpoint->link;

    assert(aTimeoutIsValid(timeout));
    assert(link->pending[index] > 0U);
    assert(link->writes[index] > 1U); /* 部分 write 补齐后只 flush 一次。 */
    link->flushes[index]++;
    if (index == 0U && link->fail_flush) return A_STATUS_ERROR;
    link->pending[index] = 0U;
    return A_STATUS_OK;
}
#endif

typedef struct {
    aModbusRole_t role;
    aModbusTransportType_t transport_type;
    uint8_t unit_id; /**< RTU 从站为 1..247；主站目标在每次请求中设置。 */
    aModbusTransport_t transport;
    aBusHandle_t *bus; /**< 必填，已初始化；协议模块不销毁。 */
    aTimeout_t byte_timeout; /**< 每次传输的字节等待限制，默认 20 ms。 */
    const aModbusMappingAddressRange_t *ranges; /**< 可选从站回调段；借用至销毁。 */
    size_t range_count;
    /** 回调段未命中时查找的离散映射；与 ranges 平级，借用至销毁。
     * 两类配置分别排序，初始化拒绝重叠。批量访问先检查完整覆盖和权限，
     * 再执行回调或 SIG 读写；跨回调/SIG 不提供整体回滚。
     */
    const aModbusServerMap_t *maps;
    size_t map_count;
    aCommValueReadFn_t sig_read;
    aCommValueWriteFn_t sig_write;
    void *sig_context;
    /** 可选整组快照工作区；NULL 使用实例内 250 字节工作区。
     * 映射的 SIG 大于 250 字节时必填，容量须覆盖最大 SIG。
     * 由实例独占，生命周期覆盖实例；不得与总线数据区重叠。
     * 内部以字节访问，不要求业务数据对齐。
     */
    void *snapshot;
    size_t snapshot_size;
#if AMODBUS_CLIENT_ENABLE
    const aModbusMappingClientConfig_t *client_override;
#endif
#if AMODBUS_SERVER_ENABLE
    const aModbusMappingServerConfig_t *server_override;
#endif
} fixtureConfig_t;
static void fixtureConfigInit(fixtureConfig_t *config)
{
    *config = (fixtureConfig_t){
        .role = AMODBUS_SERVER_ENABLE ? AMODBUS_ROLE_SERVER : AMODBUS_ROLE_CLIENT,
        .transport_type = AMODBUS_TRANSPORT_RTU, .unit_id = 1U,
        .byte_timeout = A_TIMEOUT_MS(20U)
    };
}

typedef struct {
    aModbusHandle_t *protocol;
    aModbusMapping_t *mapping;
    aBusEndpoint_t data;
    aBool_t used;
#if ABUS_EVENT_ENABLE
    aBusEndpointSubscription_t nodes[64];
    aCommBindingSubscription_t subscriptions[64];
#endif
} fixtureEndpoints_t;
static fixtureEndpoints_t endpoint_storage[8];
static aModbusHandle_t *fixtureProtocol(const aModbusMapping_t *mapping)
{
    for (size_t i = 0U; i < 8U; i++) {
        if (endpoint_storage[i].mapping == mapping && endpoint_storage[i].used)
            return endpoint_storage[i].protocol;
    }
    return NULL;
}
static aModbusMappingConfig_t fixtureConnection(const fixtureConfig_t *config,
    fixtureEndpoints_t *endpoints)
{
    const aModbusMappingConfig_t connection = {
        .protocol = aModbusCommEndpoint(endpoints->protocol),
        .comm = {
            .data = config->bus == NULL ? NULL : &endpoints->data.base,
            .sig_read = config->sig_read, .sig_write = config->sig_write,
            .sig_context = config->sig_context,
            .snapshot = config->snapshot,
            .snapshot_size = config->snapshot_size
        }
    };
    return connection;
}

/* 测试夹具显式装配两个模块；被测库之间没有兼容别名或私有耦合。 */
#if AMODBUS_STATIC_ENABLE
static aModbusHandle_t protocol_storage[8];
#endif
static aStatus_t fixtureOpen(const fixtureConfig_t *config,
    aModbusMapping_t *storage, aModbusMapping_t **out, aBool_t dynamic)
{
    aModbusHandle_t *protocol = NULL;
    aModbusMapping_t *binding = storage;
    aStatus_t status;
    if (out != NULL) *out = NULL;
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    if (storage != NULL && storage->ready) return A_STATUS_BUSY;
    if (config->role != AMODBUS_ROLE_CLIENT &&
        config->role != AMODBUS_ROLE_SERVER) return A_STATUS_INVALID_PARAM;
#if !AMODBUS_CLIENT_ENABLE
    if (config->role == AMODBUS_ROLE_CLIENT) return A_STATUS_UNSUPPORTED;
#endif
#if !AMODBUS_SERVER_ENABLE
    if (config->role == AMODBUS_ROLE_SERVER) return A_STATUS_UNSUPPORTED;
#endif
    const aModbusConfig_t transport = {
        .transport_type = config->transport_type, .transport = config->transport,
        .byte_timeout = config->byte_timeout
    };
#if AMODBUS_STATIC_ENABLE
    if (!dynamic) {
        for (size_t i = 0U; i < 8U; i++) {
            if (!protocol_storage[i].ready) {
                protocol = &protocol_storage[i];
                break;
            }
        }
        assert(protocol != NULL);
    }
#endif
    fixtureEndpoints_t *endpoints = NULL;
    for (size_t i = 0U; i < 8U; i++) {
        if (!endpoint_storage[i].used) { endpoints = &endpoint_storage[i]; break; }
    }
    assert(endpoints != NULL);
    endpoints->protocol = NULL;
    if (config->bus != NULL) {
        assert(aBusEndpointInit(&endpoints->data, config->bus) == A_STATUS_OK);
    }
#if ABUS_EVENT_ENABLE
    if (storage != NULL) {
        if (storage->comm.subscriptions == NULL) {
            storage->comm.subscriptions = endpoints->subscriptions;
            storage->comm.subscription_capacity = 64U;
        }
        for (size_t i = 0U; i < storage->comm.subscription_capacity; i++) {
            assert(i < 64U);
            storage->comm.subscriptions[i].native = &endpoints->nodes[i];
        }
    }
#endif
    (void)dynamic;
    if (config->role == AMODBUS_ROLE_CLIENT) {
#if AMODBUS_CLIENT_ENABLE
        const aModbusClientConfig_t core = {.modbus = transport};
#if AMODBUS_DYNAMIC_ENABLE
        if (dynamic) status = aModbusClientCreate(&core, &protocol);
        else
#endif
#if AMODBUS_STATIC_ENABLE
            status = aModbusClientInitStatic(&core, protocol);
#else
            status = A_STATUS_UNSUPPORTED;
#endif
        if (status != A_STATUS_OK) return status;
        aModbusMappingClientConfig_t client;
        aModbusMappingClientConfigStructInit(&client);
        if (config->client_override != NULL) client = *config->client_override;
        endpoints->protocol = protocol;
        client.binding = fixtureConnection(config, endpoints);
#if AMODBUS_DYNAMIC_ENABLE
        if (dynamic) status = aModbusMappingClientCreate(&client, &binding);
        else
#endif
#if AMODBUS_STATIC_ENABLE
            status = aModbusMappingClientInitStatic(&client, binding);
#else
            status = A_STATUS_UNSUPPORTED;
#endif
#else
        status = A_STATUS_UNSUPPORTED;
#endif
    } else {
#if AMODBUS_SERVER_ENABLE
        aModbusServerConfig_t core = {
            .modbus = transport, .unit_id = config->unit_id,
            .read = aModbusMappingServerRead, .write = aModbusMappingServerWrite
        };
        status = aModbusServerConfigCheck(&core);
        if (status != A_STATUS_OK) return status;
        aModbusMappingServerConfig_t server = {
            .binding = fixtureConnection(config, endpoints),
            .maps = config->maps, .map_count = config->map_count,
            .ranges = config->ranges, .range_count = config->range_count
        };
        if (config->server_override != NULL) server = *config->server_override;
        server.binding = fixtureConnection(config, endpoints);
#if AMODBUS_DYNAMIC_ENABLE
        if (dynamic) status = aModbusMappingServerCreate(&server, &binding);
        else
#endif
#if AMODBUS_STATIC_ENABLE
            status = aModbusMappingServerInitStatic(&server, binding);
#else
            status = A_STATUS_UNSUPPORTED;
#endif
        if (status != A_STATUS_OK) return status;
        core.context = binding;
#if AMODBUS_DYNAMIC_ENABLE
        if (dynamic) status = aModbusServerCreate(&core, &protocol);
        else
#endif
#if AMODBUS_STATIC_ENABLE
            status = aModbusServerInitStatic(&core, protocol);
#else
            status = A_STATUS_UNSUPPORTED;
#endif
        if (status != A_STATUS_OK) {
#if AMODBUS_DYNAMIC_ENABLE
            if (dynamic) (void)aModbusMappingDestroy(binding);
            else
#endif
#if AMODBUS_STATIC_ENABLE
                (void)aModbusMappingDeInitStatic(binding);
#else
                (void)binding;
#endif
            return status;
        }
        endpoints->protocol = protocol;
#else
        status = A_STATUS_UNSUPPORTED;
#endif
    }
    if (status != A_STATUS_OK) {
#if AMODBUS_DYNAMIC_ENABLE
        if (dynamic) (void)aModbusDestroy(protocol);
        else
#endif
#if AMODBUS_STATIC_ENABLE
            (void)aModbusDeInitStatic(protocol);
#else
            (void)protocol;
#endif
        return status;
    }
    endpoints->mapping = binding;
    endpoints->used = A_TRUE;
    if (out != NULL) *out = binding;
    return A_STATUS_OK;
}

#if AMODBUS_STATIC_ENABLE
static aStatus_t fixtureDeInitStatic(aModbusMapping_t *handle)
{
    aModbusHandle_t *protocol = handle == NULL ? NULL : fixtureProtocol(handle);
    fixtureEndpoints_t *endpoints = handle == NULL ? NULL :
        (fixtureEndpoints_t *)((char *)handle->config->comm.data -
        offsetof(fixtureEndpoints_t, data));
    aStatus_t status = aModbusMappingDeInitStatic(handle);
    if (status != A_STATUS_OK) return status;
    status = aModbusDeInitStatic(protocol);
    if (status == A_STATUS_OK) endpoints->used = A_FALSE;
    return status;
}
#endif
#if AMODBUS_DYNAMIC_ENABLE
static aStatus_t fixtureDestroy(aModbusMapping_t *handle)
{
    if (handle == NULL) return A_STATUS_OK;
    aModbusHandle_t *protocol = fixtureProtocol(handle);
    fixtureEndpoints_t *endpoints = (fixtureEndpoints_t *)((char *)handle->config->comm.data -
        offsetof(fixtureEndpoints_t, data));
    aStatus_t status = aModbusMappingDestroy(handle);
    if (status != A_STATUS_OK) return status;
    status = aModbusDestroy(protocol);
    if (status == A_STATUS_OK) endpoints->used = A_FALSE;
    return status;
}
#endif

#if AMODBUS_STATIC_ENABLE
static aStatus_t fixtureInitStatic(const fixtureConfig_t *config,
    aModbusMapping_t *storage)
{
    *storage = (aModbusMapping_t){0};
    return fixtureOpen(config, storage, NULL, A_FALSE);
}
#endif
#if AMODBUS_DYNAMIC_ENABLE
static aStatus_t fixtureCreate(const fixtureConfig_t *config,
    aModbusMapping_t **out)
{
    return fixtureOpen(config, NULL, out, A_TRUE);
}
#endif

static fixtureConfig_t configuration(endpoint_t *endpoint,
                                     aModbusRole_t role)
{
    fixtureConfig_t config;
    fixtureConfigInit(&config);
    config.role = role;
    config.transport_type = endpoint->link->type;
    config.bus = &bus;
    config.transport.context = endpoint;
    config.transport.stream.context = endpoint;
    config.transport.stream.read = port_read;
    config.transport.stream.write = port_write;
    config.transport.discard_input = port_discard;
    config.transport.prepare_frame = port_prepare;
    config.transport.wait_transmit_complete = port_complete;
    if (role == AMODBUS_ROLE_SERVER) {
        config.maps = maps;
        config.map_count = sizeof(maps) / sizeof(maps[0]);
        config.ranges = ranges;
        config.range_count = sizeof(ranges) / sizeof(ranges[0]);
    }
    return config;
}

static aModbusMapping_t *open_instance(const fixtureConfig_t *config,
                                     aModbusMapping_t *storage)
{
#if AMODBUS_STATIC_ENABLE
    assert(fixtureInitStatic(config, storage) == A_STATUS_OK);
    return storage;
#else
    aModbusMapping_t *handle;
    (void)storage;
    assert(fixtureCreate(config, &handle) == A_STATUS_OK);
    return handle;
#endif
}

static void close_instance(aModbusMapping_t *handle)
{
#if AMODBUS_STATIC_ENABLE
    assert(fixtureDeInitStatic(handle) == A_STATUS_OK);
#else
    assert(fixtureDestroy(handle) == A_STATUS_OK);
#endif
}

static aStatus_t invalid_config(const fixtureConfig_t *config)
{
#if AMODBUS_STATIC_ENABLE
    aModbusMapping_t storage;
    return fixtureInitStatic(config, &storage);
#else
    aModbusMapping_t *handle = (void *)1;
    aStatus_t status = fixtureCreate(config, &handle);
    assert(handle == NULL);
    return status;
#endif
}

static void lifecycle(void)
{
    link_t link = {.type = AMODBUS_TRANSPORT_RTU};
    endpoint_t endpoint = {&link, A_TRUE};
    aModbusMapping_t storage;
    fixtureConfig_t config = configuration(&endpoint,
#if AMODBUS_SERVER_ENABLE
        AMODBUS_ROLE_SERVER
#else
        AMODBUS_ROLE_CLIENT
#endif
    );
    aModbusMapping_t *handle = open_instance(&config, &storage);
    config.sig_read = sig_read;
    config.sig_write = sig_write;
    config.sig_context = &bus;
    close_instance(handle);
    assert(invalid_config(NULL) == A_STATUS_INVALID_PARAM);
    config.transport.wait_transmit_complete = NULL;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    config.transport.wait_transmit_complete = port_complete;
    config.role = (aModbusRole_t)99;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
#if AMODBUS_DYNAMIC_ENABLE
    config = configuration(&endpoint,
        AMODBUS_SERVER_ENABLE ? AMODBUS_ROLE_SERVER : AMODBUS_ROLE_CLIENT);
    testFailAllocation(A_TRUE);
    handle = (void *)1;
    assert(fixtureCreate(&config, &handle) == A_STATUS_NO_MEMORY);
    assert(handle == NULL);
    testFailAllocation(A_FALSE);
    assert(fixtureCreate(&config, &handle) == A_STATUS_OK);
#if AMODBUS_STATIC_ENABLE
    assert(fixtureDeInitStatic(handle) == A_STATUS_INVALID_PARAM);
#endif
    assert(fixtureDestroy(handle) == A_STATUS_OK);
    assert(fixtureDestroy(NULL) == A_STATUS_OK);
#endif
#if !AMODBUS_CLIENT_ENABLE
    config.role = AMODBUS_ROLE_CLIENT;
    assert(invalid_config(&config) == A_STATUS_UNSUPPORTED);
#endif
#if !AMODBUS_SERVER_ENABLE
    config.role = AMODBUS_ROLE_SERVER;
    assert(invalid_config(&config) == A_STATUS_UNSUPPORTED);
#endif
}

#if AMODBUS_CLIENT_ENABLE && AMODBUS_SERVER_ENABLE
static aStatus_t read_remote(aModbusMapping_t *client, aModbusArea_t area,
    uint16_t address, uint16_t quantity, void *data, size_t size,
    aModbusResult_t *result)
{
    aModbusClientReadRequest_t request;
    aModbusClientReadRequestStructInit(&request);
    request.access.area = area;
    request.access.address = address;
    request.access.quantity = quantity;
    request.access.data = data;
    request.access.size = size;
    request.result = result;
    return aModbusClientRead(fixtureProtocol(client), &request);
}

static aStatus_t write_remote(aModbusMapping_t *client, aModbusArea_t area,
    uint16_t address, uint16_t quantity, const void *data, size_t size,
    aModbusResult_t *result)
{
    aModbusClientWriteRequest_t request;
    aModbusClientWriteRequestStructInit(&request);
    request.access.area = area;
    request.access.address = address;
    request.access.quantity = quantity;
    request.access.data = data;
    request.access.size = size;
    request.result = result;
    return aModbusClientWrite(fixtureProtocol(client), &request);
}

static void stream_tests(void)
{
    for (unsigned scenario = 0U; scenario < 9U; scenario++) {
        link_t link = {
            .type = scenario == 0U || scenario == 4U ||
                    scenario == 5U || scenario == 8U ?
                    AMODBUS_TRANSPORT_RTU : AMODBUS_TRANSPORT_TCP,
            .buffered = A_TRUE,
            .fail_flush = scenario == 2U || scenario == 4U,
            .fail_write = scenario == 5U,
            .eof = scenario == 3U || scenario == 8U,
            .eof_after = scenario == 6U ? 2U : 0U,
            .zero_write = scenario == 7U,
        };
        endpoint_t server_port = {&link, A_TRUE};
        endpoint_t client_port = {&link, A_FALSE};
        fixtureConfig_t server_config =
            configuration(&server_port, AMODBUS_ROLE_SERVER);
        fixtureConfig_t client_config =
            configuration(&client_port, AMODBUS_ROLE_CLIENT);
        aModbusMapping_t server_storage, client_storage;
        aModbusMapping_t *client;
        aStatus_t status;
        uint16_t value = 0xBCDE;
        aCommChannel_t *channels[2] = {NULL, NULL};
#if ACOMM_STATIC_ENABLE
        aCommChannel_t channel_storage[2];
#endif

        if (link.type == AMODBUS_TRANSPORT_TCP) {
            /* TCP 只需要 Stream，不借用任何 RTU 辅助回调或 context。 */
            aModbusTransportStructInit(&server_config.transport);
            aModbusTransportStructInit(&client_config.transport);
            server_config.transport.stream.context = &server_port;
            server_config.transport.stream.read = port_read;
            server_config.transport.stream.write = port_write;
            client_config.transport.stream.context = &client_port;
            client_config.transport.stream.read = port_read;
            client_config.transport.stream.write = port_write;
        }
        server_config.transport.stream.flush = stream_flush;
        client_config.transport.stream.flush = stream_flush;
        if (link.type == AMODBUS_TRANSPORT_TCP) {
            fixtureConfig_t *configs[] = {&server_config, &client_config};
            for (size_t i = 0U; i < 2U; i++) {
                aCommChannelConfig_t channel;
                aCommChannelConfigStructInit(&channel);
                channel.stream = configs[i]->transport.stream;
#if ACOMM_STATIC_ENABLE
                channels[i] = &channel_storage[i];
                assert(aCommChannelInitStatic(&channel, channels[i]) ==
                       A_STATUS_OK);
#else
                assert(aCommChannelCreate(&channel, &channels[i]) ==
                       A_STATUS_OK);
#endif
                assert(aModbusBindChannel(channels[i],
                    &configs[i]->transport) == A_STATUS_OK);
            }
        }
        link.server = open_instance(&server_config, &server_storage);
        client = open_instance(&client_config, &client_storage);
        status = read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
                             0U, 1U, &value, sizeof(value), NULL);
        assert(link.flushes[0] ==
               (scenario == 5U || scenario == 7U ? 0U : 1U));
        if (scenario < 2U) {
            assert(status == A_STATUS_OK && value == word);
            assert(link.flushes[1] == 1U);
            assert(link.completed == (scenario == 0U ? 2U : 0U));
        } else {
            assert(status == (link.eof || link.eof_after != 0U ?
                              A_STATUS_EOF : A_STATUS_ERROR));
            assert(value == 0xBCDE);
            assert(link.flushes[1] == (scenario == 6U ? 1U : 0U));
            assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
                0U, 1U, &value, sizeof(value), NULL) == A_STATUS_NOT_READY);
        }
        close_instance(client);
        close_instance(link.server);
        for (size_t i = 0U; i < 2U; i++) {
            if (channels[i] == NULL) continue;
#if ACOMM_STATIC_ENABLE
            assert(aCommChannelDeInitStatic(channels[i]) == A_STATUS_OK);
#else
            assert(aCommChannelDestroy(channels[i]) == A_STATUS_OK);
#endif
        }
    }
}

static void server_eof(void)
{
    link_t link = {.type = AMODBUS_TRANSPORT_TCP, .eof_server = A_TRUE};
    endpoint_t endpoint = {&link, A_TRUE};
    fixtureConfig_t config = configuration(&endpoint, AMODBUS_ROLE_SERVER);
    aModbusMapping_t storage;
    aModbusMapping_t *server = open_instance(&config, &storage);
    aModbusServerProcessRequest_t request;
    aModbusServerProcessRequestStructInit(&request);
    assert(aModbusServerProcess(fixtureProtocol(server), &request) == A_STATUS_EOF);
    assert(aModbusServerProcess(fixtureProtocol(server), &request) == A_STATUS_NOT_READY);
    assert(link.response_size == 0U);
    close_instance(server);
}

static void end_to_end(aModbusTransportType_t type, aBool_t callbacks)
{
    link_t link = {.type = type};
    endpoint_t server_port = {&link, A_TRUE};
    endpoint_t client_port = {&link, A_FALSE};
    fixtureConfig_t server_config =
        configuration(&server_port, AMODBUS_ROLE_SERVER);
    fixtureConfig_t client_config =
        configuration(&client_port, AMODBUS_ROLE_CLIENT);
    aModbusMapping_t server_storage;
    aModbusMapping_t client_storage;
    aModbusMapping_t *client;
    aModbusResult_t result;
    uint16_t registers[12];
    uint8_t bits[2] = {0xA5, 0x03};
    uint8_t read_bits[3] = {0, 0, 0xCC};
    aModbusMappingClientSigRequest_t sig_request;

    word = 123;
    counter = 0x12345678U;
    signed_value = -42;
    motor = motor_default;
    memcpy(raw, raw_default, sizeof(raw));
    other_counter = 0x89ABCDEFU;
    if (callbacks) {
        server_config.sig_read = sig_read;
        server_config.sig_write = sig_write;
        server_config.sig_context = &bus;
    }
    link.server = open_instance(&server_config, &server_storage);
    callback_instance = callbacks ? link.server : NULL;
    client = open_instance(&client_config, &client_storage);
    assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        0, 12, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(registers[0] == 123 && registers[1] == 0x1234 &&
           registers[2] == 0x5678 && registers[3] == 0xFFFF &&
           registers[4] == 0xFFD6 && registers[5] == 0 &&
           registers[6] == 3000 && registers[7] == 0x1122 &&
           registers[8] == 0x3344 && registers[9] == 0x5500 &&
           registers[10] == 0x89AB && registers[11] == 0xCDEF);
    assert(read_remote(client, AMODBUS_AREA_INPUT_REGISTERS,
        0, 2, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(registers[0] == 0x5678 && registers[1] == 0x1234);
    assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        2, 1, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(registers[0] == 0x5678);
    assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        1, 1, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(registers[0] == 0x1234);
    registers[0] = 0xCAFE;
    registers[1] = 0xBABE;
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        1, 2, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(counter == 0xCAFEBABEU);
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        2, 1, registers, sizeof(registers), &result) == A_STATUS_ERROR);
    assert(result.exception == 2U && counter == 0xCAFEBABEU);
    /* 从高字开始但长度不足，同样禁止半值写入。 */
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        1, 1, registers, sizeof(registers), &result) == A_STATUS_ERROR);
    assert(result.exception == 2U && counter == 0xCAFEBABEU);
    registers[0] = 999;
    registers[1] = 0;
    registers[2] = 111;
    registers[3] = 0;
    registers[4] = 0;
    registers[5] = 0;
    registers[6] = 6001;
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        0, 7, registers, sizeof(registers), &result) == A_STATUS_ERROR);
    assert(result.exception == 3U && word == 123 && counter == 0xCAFEBABEU);
    registers[0] = 0;
    registers[1] = 5500;
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        5, 2, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(motor.speed == 5500 && motor.temperature == 25);
    registers[0] = 0xAABB;
    registers[1] = 0xCCDD;
    registers[2] = 0xEE00;
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        7, 3, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(raw[0] == 0xAA && raw[4] == 0xEE);
    /* RAW 允许读取其中一个寄存器，写入必须覆盖完整映射。 */
    assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        8, 1, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(registers[0] == 0xCCDDU);
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        7, 2, registers, sizeof(registers), &result) == A_STATUS_ERROR);
    assert(result.exception == 2U && raw[0] == 0xAA && raw[4] == 0xEE);
    registers[0] = 0xAABB;
    registers[2] = 0xEE01;
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        7, 3, registers, sizeof(registers), &result) == A_STATUS_ERROR);
    assert(result.exception == 3U && raw[4] == 0xEE);
    assert(write_remote(client, AMODBUS_AREA_COILS,
        0, 10, bits, sizeof(bits), &result) == A_STATUS_OK);
    assert(read_remote(client, AMODBUS_AREA_COILS,
        0, 10, read_bits, 2, &result) == A_STATUS_OK);
    assert(read_bits[0] == 0xA5 && (read_bits[1] & 3U) == 3U &&
           read_bits[2] == 0xCC);
    assert(read_remote(client, AMODBUS_AREA_DISCRETE_INPUTS,
        0, 10, read_bits, 2, &result) == A_STATUS_OK);
    bits[0] = 0;
    assert(write_remote(client, AMODBUS_AREA_COILS,
        0, 1, bits, 1, &result) == A_STATUS_OK && flags[0] == 0);
    assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        12, 1, registers, sizeof(registers), &result) == A_STATUS_ERROR);
    assert(result.exception == 2U);
    aModbusMappingClientSigRequestStructInit(&sig_request);
    sig_request.area = AMODBUS_AREA_HOLDING_REGISTERS;
    sig_request.address = 1;
    sig_request.target.deviceID = 1;
    sig_request.target.sigIndex = LOCAL;
    assert(aModbusMappingClientReadSig(client, &sig_request) == A_STATUS_OK);
    assert(local_value == counter);
    local_value = 12345;
    assert(aModbusMappingClientWriteSig(client, &sig_request) == A_STATUS_OK);
    assert(counter == 12345);
    sig_request.address = 0;
    sig_request.area = AMODBUS_AREA_COILS;
    sig_request.target.sigIndex = 0;
    assert(aModbusMappingClientReadSig(client, &sig_request) == A_STATUS_OK);
    if (callbacks) {
        reject_sig_write = A_TRUE;
        registers[0] = 777;
        assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
            0, 1, registers, sizeof(registers), &result) == A_STATUS_ERROR);
        assert(result.exception == 4U && word == 123);
        reject_sig_write = A_FALSE;
        assert(sig_reads != 0U && sig_writes != 0U);
    }
    if (type == AMODBUS_TRANSPORT_RTU) {
        aModbusClientWriteRequest_t broadcast;
        aModbusClientWriteRequestStructInit(&broadcast);
        registers[0] = 321;
        broadcast.unit_id = 0;
        broadcast.access.data = registers;
        broadcast.access.size = sizeof(registers);
        broadcast.access.quantity = 1;
        assert(aModbusClientWrite(fixtureProtocol(client), &broadcast) == A_STATUS_OK);
        assert(word == 321 && link.response_size == 0U);
        link.corrupt = A_TRUE;
        assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
            0, 1, registers, sizeof(registers), &result) == A_STATUS_ERROR);
        link.corrupt = A_FALSE;
        assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
            0, 1, registers, sizeof(registers), &result) == A_STATUS_OK);
    } else {
        link.short_tcp = A_TRUE;
        assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
            0, 1, registers, sizeof(registers), &result) == A_STATUS_ERROR);
        link.short_tcp = A_FALSE;
        assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
            0, 1, registers, sizeof(registers), &result) == A_STATUS_NOT_READY);
    }
    assert(link.completed != 0U && link.prepared != 0U);
    close_instance(client);
    close_instance(link.server);
    callback_instance = NULL;
    assert(address_reads != 0U && address_writes != 0U);
}

/* 分组/发布测试统一经过正式 Plan 路径，配置变化后重新建立计划。 */
static aStatus_t execute_first_plan(aModbusMapping_t *handle,
    const fixturePlanConfig_t *config, size_t *count, aModbusResult_t *result)
{
    aCommPlan_t *plan;
    aStatus_t status;
    aCommEndpointResult_t endpoint_result;
    const aCommPlanRequest_t request = {.result = &endpoint_result};
    if (count != NULL) *count = 0U;
#if AMODBUS_STATIC_ENABLE
    aCommPlanFrame_t frames[8];
    size_t indices[8];
    aCommPlan_t storage = {
        .frames = frames, .indices = indices,
        .capacity = 8U, .index_capacity = 8U
    };
    status = fixturePlanInitStatic(handle, config, &storage);
    plan = &storage;
#else
    status = fixturePlanCreate(handle, config, &plan);
#endif
    if (status != A_STATUS_OK) return status;
    assert(aCommPlanFrameCount(plan) != 0U);
    if (count != NULL) *count = plan->frames[0].count;
    status = aCommPlanExecute(plan, &request);
    if (result != NULL) *result = (aModbusResult_t){
        endpoint_result.exception, endpoint_result.started, endpoint_result.retryable
    };
#if AMODBUS_STATIC_ENABLE
    assert(aCommPlanDeInitStatic(plan) == A_STATUS_OK);
#else
    assert(aCommPlanDestroy(plan) == A_STATUS_OK);
#endif
    return status;
}

static void scheduled_test(void *context, size_t step,
    aCommStepResult_t *result)
{
    const aCommPlanRequest_t request = {
        .frame_index = step, .execution = result
    };
    result->status = aCommPlanExecute(context, &request);
}

static void prepared_plans(aModbusTransportType_t type)
{
    aModbusClientMap_t items[] = {
        {.area = AMODBUS_AREA_HOLDING_REGISTERS, .address = 0U,
         .quantity = 1U, .comm.deviceID = 1U,
                   .comm.sigIndex = WORD,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE},
        {.area = AMODBUS_AREA_HOLDING_REGISTERS, .address = 1U,
         .quantity = 2U, .comm.deviceID = 1U,
                   .comm.sigIndex = LOCAL,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE},
        {.area = AMODBUS_AREA_HOLDING_REGISTERS, .address = 3U,
         .quantity = 2U, .comm.flags = ACOMM_MAP_FLAG_BREAK_BEFORE,
         .comm.deviceID = 1U,
                   .comm.sigIndex = SIGNED,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE},
        {.area = AMODBUS_AREA_COILS, .address = 0U,
         .quantity = 1U, .comm.deviceID = 1U,
                   .comm.sigIndex = 0U,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE},
        {.area = AMODBUS_AREA_DISCRETE_INPUTS, .address = 1U,
         .quantity = 1U, .comm.deviceID = 1U,
                   .comm.sigIndex = 1U,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE},
        {.area = AMODBUS_AREA_INPUT_REGISTERS, .address = 0U,
         .quantity = 2U, .comm.deviceID = 1U,
                   .comm.sigIndex = LOCAL,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE,
         .word_order = AMODBUS_WORD_LOW_FIRST},
        {.area = AMODBUS_AREA_HOLDING_REGISTERS, .address = 12U,
         .quantity = 2U, .comm.deviceID = 1U,
                   .comm.sigIndex = LOCAL,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE},
    };
    link_t link = {.type = type};
    endpoint_t server_port = {&link, A_TRUE};
    endpoint_t client_port = {&link, A_FALSE};
    fixtureConfig_t server_config =
        configuration(&server_port, AMODBUS_ROLE_SERVER);
    fixtureConfig_t client_config =
        configuration(&client_port, AMODBUS_ROLE_CLIENT);
    aModbusMapping_t server_storage, client_storage;
    aModbusMapping_t *client;
    fixturePlanConfig_t config;
    aCommPlanRequest_t request;
    aCommPlan_t *plan;
    aCommStepResult_t execution;
    aModbusResult_t result;
    aCommEndpointResult_t endpoint_result;
    uint16_t immediate[2];
    unsigned allocations;
#if AMODBUS_STATIC_ENABLE
    aCommPlanFrame_t frames[7];
    size_t indices[7];
    aCommPlan_t storage = {
        .frames = frames, .indices = indices, .capacity = 7U,
        .index_capacity = 7U};
#endif
    for (size_t i = 0U; i < sizeof(items) / sizeof(items[0]); i++) {
        items[i].comm.flags |= ACOMM_MAP_FLAG_READ;
        items[i].comm.poll_time_ms = 1000U;
    }
    client_config.sig_write = sig_write;
    client_config.sig_context = &bus;
    link.server = open_instance(&server_config, &server_storage);
    client = open_instance(&client_config, &client_storage);
    fixturePlanConfigInit(&config);
    config.items = items;
    config.count = sizeof(items) / sizeof(items[0]);
    config.timeout = A_TIMEOUT_MS(50U);
#if AMODBUS_STATIC_ENABLE
    storage.capacity = 1U;
    assert(fixturePlanInitStatic(client, &config, &storage) ==
           A_STATUS_INVALID_PARAM);
    assert(!storage.ready && client->plan_count == 0U);
    storage.capacity = 7U;
    storage.index_capacity = 1U;
    assert(fixturePlanInitStatic(client, &config, &storage) ==
           A_STATUS_INVALID_PARAM);
    assert(!storage.ready && client->plan_count == 0U);
    storage.index_capacity = 7U;
    assert(fixturePlanInitStatic(client, &config, &storage) == A_STATUS_OK);
    plan = &storage;
    assert(fixtureDeInitStatic(client) == A_STATUS_BUSY);
#else
    assert(fixturePlanCreate(client, &config, &plan) == A_STATUS_OK);
    assert(fixtureDestroy(client) == A_STATUS_BUSY);
#endif
    assert(aCommPlanFrameCount(plan) == 6U);
    assert(plan->frames[0].count == 2U);
    allocations = testAllocations();
    word = 123U;
    counter = 0x12345678U;
    signed_value = -42;
    flags[0] = 1U;
    flags[1] = 0U;
    aCommPlanRequestStructInit(&request);
    request.execution = &execution;
    request.result = &endpoint_result;
    for (size_t i = 0U; i < 5U; i++) {
        request.frame_index = i;
        assert(aCommPlanExecute(plan, &request) == A_STATUS_OK);
        assert(execution.advance && endpoint_result.exception == 0U);
    }
    assert(local_value == counter && signed_value == -42);
    assert(flags[0] == 1U && flags[1] == 0U);
    request.frame_index = 5U;
    assert(aCommPlanExecute(plan, &request) == A_STATUS_ERROR);
    assert(execution.advance && endpoint_result.exception == 2U);
    request.frame_index = 0U;
    /* 发布返回 BUSY 仍算本帧已尝试；实例入口重入 BUSY 则没有尝试。 */
    reject_sig_write = A_TRUE;
    assert(aCommPlanExecute(plan, &request) == A_STATUS_BUSY && execution.advance);
    reject_sig_write = A_FALSE;
    aAtomicStore(&client->active, A_TRUE);
    assert(aCommPlanExecute(plan, &request) == A_STATUS_BUSY && !execution.advance);
    aAtomicStore(&client->active, A_FALSE);
    /* 协议句柄忙和已经进入传输后的 BUSY 不能混为一类。 */
    aAtomicStore(&fixtureProtocol(client)->active, A_TRUE);
    assert(aCommPlanExecute(plan, &request) == A_STATUS_BUSY);
    assert(!execution.advance && !endpoint_result.started);
    aAtomicStore(&fixtureProtocol(client)->active, A_FALSE);
    if (type == AMODBUS_TRANSPORT_RTU) {
        link.prepare_busy = A_TRUE;
        assert(aCommPlanExecute(plan, &request) == A_STATUS_BUSY);
        assert(execution.advance && endpoint_result.started && !execution.retryable);
        link.prepare_busy = A_FALSE;
    }
    assert(aCommPlanExecute(plan, &request) == A_STATUS_OK);
    /* 即时读写不改预生成帧；随后继续执行同一计划。 */
    assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        1U, 2U, immediate, sizeof(immediate), &result) == A_STATUS_OK);
    assert(aCommPlanExecute(plan, &request) == A_STATUS_OK);
    assert(local_value == counter);
    if (type == AMODBUS_TRANSPORT_RTU) {
        static const uint8_t expected[] = {1U, 3U, 0U, 0U, 0U, 3U};
        assert(memcmp(link.request, expected, sizeof(expected)) == 0);
        aModbusPreparedRead_t prepared;
        memcpy(&prepared, plan->frames[0].request.data, sizeof(prepared));
        assert(memcmp(prepared.rtu, link.request, 8U) == 0);
        link.corrupt = A_TRUE;
        assert(aCommPlanExecute(plan, &request) == A_STATUS_ERROR);
        link.corrupt = A_FALSE;
        assert(aCommPlanExecute(plan, &request) == A_STATUS_OK);
        for (unsigned fault = 1U; fault <= 4U; fault++) {
            link.invalid_response = fault;
            assert(aCommPlanExecute(plan, &request) ==
                   (fault == 4U ? A_STATUS_TIMEOUT : A_STATUS_ERROR));
            assert(execution.advance);
        }
        link.invalid_response = 0U;
        assert(aCommPlanExecute(plan, &request) == A_STATUS_OK);
        link.delay = 60U;
        assert(aCommPlanExecute(plan, &request) == A_STATUS_TIMEOUT);
        assert(execution.advance);
        link.delay = 0U;
        assert(aCommPlanExecute(plan, &request) == A_STATUS_OK);
    }
    assert(testAllocations() == allocations);
#if AMODBUS_STATIC_ENABLE
    assert(aCommPlanDeInitStatic(plan) == A_STATUS_OK);
#else
    assert(aCommPlanDestroy(plan) == A_STATUS_OK);
#endif
#if AMODBUS_DYNAMIC_ENABLE
    testFailAllocation(A_TRUE);
    assert(fixturePlanCreate(client, &config, &plan) == A_STATUS_NO_MEMORY);
    assert(plan == NULL && client->plan_count == 0U);
    testFailAllocation(A_FALSE);
    assert(fixturePlanCreate(client, &config, &plan) == A_STATUS_OK);
    assert(aCommPlanDestroy(plan) == A_STATUS_OK);
#endif
    /* 不轮询项不进入计划；按周期稳定分组，一次性独立成组。 */
    items[0].comm.poll_time_ms = 0U;
    items[2].comm.poll_time_ms = ACOMM_POLL_ONCE;
    items[4].comm.poll_time_ms = 0U;
    items[5].comm.poll_time_ms = 2000U;
#if AMODBUS_STATIC_ENABLE
    assert(fixturePlanInitStatic(client, &config, &storage) == A_STATUS_OK);
    plan = &storage;
#else
    assert(fixturePlanCreate(client, &config, &plan) == A_STATUS_OK);
#endif
    assert(aCommPlanGroupCount(plan) == 3U);
    assert(aCommPlanFrameCount(plan) == 5U);
    aCommPlanGroup_t group;
    assert(aCommPlanGroupGet(plan, 0U, &group) == A_STATUS_OK);
    assert(group.poll_time_ms == 1000U && group.frame_count == 3U);
    assert(aCommPlanGroupGet(plan, 1U, &group) == A_STATUS_OK);
    assert(group.poll_time_ms == ACOMM_POLL_ONCE && group.frame_count == 1U);
    /* 跳过禁用项并按周期重排后，仍读取原地址并发布到对应 SIG。 */
    const uint16_t addresses[] = {1U, 0U, 12U, 3U, 0U};
    counter = 0x89abcdefU;
    local_value = 0U;
    for (size_t i = 0U; i < 5U; i++) {
        request.frame_index = i;
        assert(aCommPlanExecute(plan, &request) ==
               (i == 2U ? A_STATUS_ERROR : A_STATUS_OK));
        size_t offset = type == AMODBUS_TRANSPORT_RTU ? 2U : 8U;
        uint16_t address = (uint16_t)(link.request[offset] << 8U) |
                           link.request[offset + 1U];
        assert(address == addresses[i] && execution.advance);
    }
    assert(local_value == counter && signed_value == -42);
#if AMODBUS_STATIC_ENABLE
    assert(aCommPlanDeInitStatic(plan) == A_STATUS_OK);
#else
    assert(aCommPlanDestroy(plan) == A_STATUS_OK);
#endif

    for (size_t i = 0U; i < config.count; i++) items[i].comm.poll_time_ms = 0U;
#if AMODBUS_STATIC_ENABLE
    assert(fixturePlanInitStatic(client, &config, &storage) == A_STATUS_OK);
    plan = &storage;
#else
    assert(fixturePlanCreate(client, &config, &plan) == A_STATUS_OK);
#endif
    assert(aCommPlanGroupCount(plan) == 0U);
    assert(aCommPlanFrameCount(plan) == 0U);
#if AMODBUS_STATIC_ENABLE
    assert(aCommPlanDeInitStatic(plan) == A_STATUS_OK);
#else
    assert(aCommPlanDestroy(plan) == A_STATUS_OK);
#endif
    items[0].comm.flags = ACOMM_MAP_FLAG_WRITE;
    items[0].comm.poll_time_ms = ACOMM_POLL_ONCE;
    assert(execute_first_plan(client, &config, NULL, NULL) ==
           A_STATUS_INVALID_PARAM);
    items[0].comm.flags = ACOMM_MAP_FLAG_READ;

    /* 重试次数由通用作业配置。 */
    /* 单个一次性帧：成功立即停止，超时重试两次后停止。 */
    if (type == AMODBUS_TRANSPORT_RTU) {
        for (unsigned failure = 0U; failure < 2U; failure++) {
            for (size_t i = 0U; i < config.count; i++) {
                items[i].comm.poll_time_ms = i == 0U ? ACOMM_POLL_ONCE : 0U;
            }
#if AMODBUS_STATIC_ENABLE
            assert(fixturePlanInitStatic(client, &config, &storage) == A_STATUS_OK);
            plan = &storage;
#else
            assert(fixturePlanCreate(client, &config, &plan) == A_STATUS_OK);
#endif
            const aCommJobConfig_t job = {
                .execute = scheduled_test, .context = plan, .step_count = 1U,
                .poll_time_ms = ACOMM_POLL_ONCE, .error_delay_ms = 5U,
                .retry_count = 2U
            };
            const aCommConfig_t poll_config = {.jobs = &job, .job_count = 1U};
            aCommHandle_t *scheduler;
#if ACOMM_DYNAMIC_ENABLE
            assert(aCommCreate(&poll_config, &scheduler) == A_STATUS_OK);
#else
            aCommJobState_t job_state;
            aCommHandle_t scheduler_storage = {
                .jobs = &job_state, .job_capacity = 1U
            };
            assert(aCommInitStatic(&poll_config, &scheduler_storage) ==
                   A_STATUS_OK);
            scheduler = &scheduler_storage;
#endif
            aCommProcessResult_t processed;
            link.delay = failure ? 60U : 0U;
            for (unsigned i = 0U; i < (failure ? 3U : 1U); i++) {
                assert(aCommProcess(scheduler, &processed) == A_STATUS_OK);
                assert(processed.type == ACOMM_WORK_JOB);
                assert(processed.execution.status ==
                       (failure ? A_STATUS_TIMEOUT : A_STATUS_OK));
                assert(processed.execution.advance == (!failure || i == 2U));
                testAdvanceTime(5U);
            }
            assert(aCommJobTrigger(scheduler, 0U) == A_STATUS_OK);
            testAdvanceTime(5000U);
            assert(aCommProcess(scheduler, &processed) == A_STATUS_OK);
            assert(processed.type == ACOMM_WORK_NONE);
            assert(processed.wait.type == A_TIMEOUT_TYPE_FOREVER);
#if ACOMM_DYNAMIC_ENABLE
            assert(aCommDestroy(scheduler) == A_STATUS_OK);
#else
            assert(aCommDeInitStatic(scheduler) == A_STATUS_OK);
#endif
#if AMODBUS_STATIC_ENABLE
            assert(aCommPlanDeInitStatic(plan) == A_STATUS_OK);
#else
            assert(aCommPlanDestroy(plan) == A_STATUS_OK);
#endif
            link.delay = 0U;
        }
    }
    close_instance(client);
    close_instance(link.server);
}

static aStatus_t maximum_read(void *context,
    const aModbusAddressReadRequest_t *request)
{
    (void)context;
    if (request->area == AMODBUS_AREA_COILS) {
        uint8_t *bits = request->data;
        memset(bits, 0, request->size);
        for (size_t i = 0U; i < request->quantity; i++) {
            nmbs_bitfield_write(bits, i, ((request->address + i) & 1U) != 0U);
        }
    } else {
        uint16_t *registers = request->data;
        for (size_t i = 0U; i < request->quantity; i++) {
            registers[i] = (uint16_t)(request->address + i);
        }
    }
    return A_STATUS_OK;
}

static aStatus_t maximum_write(void *context,
    const aModbusAddressWriteRequest_t *request)
{
    (void)context;
    if (request->area == AMODBUS_AREA_COILS) {
        const uint8_t *bits = request->data;
        assert(request->quantity == 1968U);
        assert(nmbs_bitfield_read(bits, 1967U));
    } else {
        const uint16_t *registers = request->data;
        assert(request->quantity == 123U);
        assert(registers[122] == 0xABCD);
    }
    return A_STATUS_OK;
}

static void maximum_frames(aModbusTransportType_t type)
{
    static const aModbusMappingAddressRange_t maximum_ranges[] = {
        {.area = AMODBUS_AREA_COILS, .quantity = 2000, .flags = READ_WRITE,
         .read = maximum_read, .write = maximum_write},
        {.area = AMODBUS_AREA_HOLDING_REGISTERS, .quantity = 65536,
         .flags = READ_WRITE, .read = maximum_read, .write = maximum_write}
    };
    link_t link = {.type = type};
    endpoint_t server_port = {&link, A_TRUE};
    endpoint_t client_port = {&link, A_FALSE};
    fixtureConfig_t server_config =
        configuration(&server_port, AMODBUS_ROLE_SERVER);
    fixtureConfig_t client_config =
        configuration(&client_port, AMODBUS_ROLE_CLIENT);
    aModbusMapping_t server_storage;
    aModbusMapping_t client_storage;
    aModbusMapping_t *client;
    uint16_t registers[125];
    uint8_t bits[251];
    aModbusResult_t result;
    server_config.maps = NULL;
    server_config.map_count = 0U;
    server_config.ranges = maximum_ranges;
    server_config.range_count = 2U;
    link.server = open_instance(&server_config, &server_storage);
    client = open_instance(&client_config, &client_storage);
    memset(bits, 0xFF, sizeof(bits));
    assert(read_remote(client, AMODBUS_AREA_COILS,
        0, 2000, bits, 250, &result) == A_STATUS_OK);
    assert(bits[0] == 0xAA && bits[249] == 0xAA && bits[250] == 0xFF);
    assert(write_remote(client, AMODBUS_AREA_COILS,
        0, 1968, bits, 246, &result) == A_STATUS_OK);
    assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        65411, 125, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(registers[0] == 65411 && registers[124] == UINT16_MAX);
    registers[122] = 0xABCD;
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        65413, 123, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        0, 124, registers, sizeof(registers), &result) ==
        A_STATUS_INVALID_PARAM);
    assert(read_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        65535, 2, registers, sizeof(registers), &result) ==
        A_STATUS_INVALID_PARAM);
    close_instance(client);
    close_instance(link.server);
}

static void malformed_frames(void)
{
    link_t link = {.type = AMODBUS_TRANSPORT_RTU};
    endpoint_t port = {&link, A_TRUE};
    fixtureConfig_t config = configuration(&port, AMODBUS_ROLE_SERVER);
    aModbusMapping_t storage;
    aModbusServerProcessRequest_t process;
    aModbusResult_t result;
    aModbusClientReadRequest_t read;
    endpoint_t client_port = {&link, A_FALSE};
    fixtureConfig_t client_config =
        configuration(&client_port, AMODBUS_ROLE_CLIENT);
    aModbusMapping_t client_storage;
    aModbusMapping_t *client;

    link.server = open_instance(&config, &storage);
    aModbusServerProcessRequestStructInit(&process);
    process.result = &result;
    assert(aModbusServerProcess(fixtureProtocol(link.server), &process) == A_STATUS_OK);
    /* 声明 255 字节负载，验证接收前的帧缓冲边界保护。 */
    memset(link.request, 0, sizeof(link.request));
    link.request[0] = 1;
    link.request[1] = 16;
    link.request[5] = 123;
    link.request[6] = 255;
    link.request_size = 264;
    assert(aModbusServerProcess(fixtureProtocol(link.server), &process) == A_STATUS_ERROR);
    link.request_size = 0;
    link.request_pos = 0;
    assert(aModbusServerProcess(fixtureProtocol(link.server), &process) == A_STATUS_OK);
    client = open_instance(&client_config, &client_storage);
    aModbusClientReadRequestStructInit(&read);
    read.access.data = &word;
    read.access.size = sizeof(word);
    read.access.quantity = 1;
    read.access.timeout = A_TIMEOUT_MS(2U);
    link.delay = 1;
    testSetTime(UINT32_MAX - 1U);
    assert(aModbusClientRead(fixtureProtocol(client), &read) == A_STATUS_TIMEOUT);
    assert(aOSGetUptimeMs() == 0U);
    close_instance(client);
    close_instance(link.server);

    link.type = AMODBUS_TRANSPORT_TCP;
    link.delay = 0;
    link.request_pos = 0;
    link.request_size = 8;
    memset(link.request, 0, sizeof(link.request));
    link.request[5] = 2;
    link.request[6] = 1;
    link.request[7] = 6;
    config = configuration(&port, AMODBUS_ROLE_SERVER);
    link.server = open_instance(&config, &storage);
    assert(aModbusServerProcess(fixtureProtocol(link.server), &process) == A_STATUS_ERROR);
    assert(aModbusServerProcess(fixtureProtocol(link.server), &process) == A_STATUS_NOT_READY);
    close_instance(link.server);

    /* 地址段重叠和整个 STRUCT 映射在初始化时拒绝。 */
    aModbusMappingAddressRange_t invalid_ranges[2] = {ranges[0], ranges[0]};
    config.ranges = invalid_ranges;
    config.range_count = 2;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    aModbusServerMap_t invalid_map = MAP(0U, 3U, MOTOR);
    config.ranges = NULL;
    config.range_count = 0U;
    config.maps = &invalid_map;
    config.map_count = 1U;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
}
#endif

int main(void)
{
    aBusInstanceStructInit(&bus, states, SIG_COUNT + 1U);
    assert(aBusInitStatic(7, tables, 2, &bus) == A_STATUS_OK);
    lifecycle();
#if AMODBUS_CLIENT_ENABLE && AMODBUS_SERVER_ENABLE
    end_to_end(AMODBUS_TRANSPORT_RTU, A_FALSE);
    end_to_end(AMODBUS_TRANSPORT_RTU, A_TRUE);
    end_to_end(AMODBUS_TRANSPORT_TCP, A_FALSE);
    end_to_end(AMODBUS_TRANSPORT_TCP, A_TRUE);
    maximum_frames(AMODBUS_TRANSPORT_RTU);
    maximum_frames(AMODBUS_TRANSPORT_TCP);
    malformed_frames();
    stream_tests();
    server_eof();
    prepared_plans(AMODBUS_TRANSPORT_RTU);
    prepared_plans(AMODBUS_TRANSPORT_TCP);
#endif
    assert(aBusDeInitStatic(&bus) == A_STATUS_OK);
    assert(testAllocations() == 0U);
#if AMODBUS_CLIENT_ENABLE && AMODBUS_SERVER_ENABLE
    puts("aModbus: lifecycle, RTU/TCP, aBus and callbacks passed");
#else
    puts("aModbus: role/allocation configuration and lifecycle passed");
#endif
    return 0;
}
