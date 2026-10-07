#include "aModbus.h"
#include "aModbus_instance.h"
#include "aBus_instance.h"
#include "aOS.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void testAdvanceTime(uint32_t milliseconds);
void testSetTime(uint32_t milliseconds);
void testFailAllocation(aBool_t fail);
unsigned testAllocations(void);

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
static aModbusHandle_t *callback_instance;
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

#define READ_WRITE (AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE)
#define MAP(addr, index) \
    {.address = addr, .flags = READ_WRITE, \
     .target = {1, index, AMODBUS_SIG_WHOLE}}
static const aModbusBusMap_t coil_maps[] = {
    MAP(0, 0), MAP(1, 1), MAP(2, 2), MAP(3, 3), MAP(4, 4)
};
static const aModbusBusMap_t holding_maps[] = {
    MAP(0, WORD), MAP(1, COUNTER), MAP(3, SIGNED),
    {.address = 5, .flags = READ_WRITE, .target = {1, MOTOR, 0}},
    MAP(7, RAW),
    {.address = 10, .flags = READ_WRITE,
     .target = {2, 0, AMODBUS_SIG_WHOLE}}
};
static const aModbusBusMap_t input_maps[] = {
    {.address = 0, .flags = AMODBUS_ACCESS_READ,
     .target = {1, COUNTER, AMODBUS_SIG_WHOLE},
     .word_order = AMODBUS_WORD_LOW_FIRST}
};
static const aModbusAddressRange_t ranges[] = {
    {.area = AMODBUS_AREA_COILS, .address = 0, .quantity = 5,
     .flags = READ_WRITE, .maps = coil_maps, .map_count = 5},
    {.area = AMODBUS_AREA_COILS, .address = 5, .quantity = 5,
     .flags = READ_WRITE, .read = address_read, .write = address_write,
     .context = &bus},
    {.area = AMODBUS_AREA_DISCRETE_INPUTS, .quantity = 10,
     .flags = AMODBUS_ACCESS_READ, .read = address_read, .context = &bus},
    {.area = AMODBUS_AREA_HOLDING_REGISTERS, .quantity = 12,
     .flags = READ_WRITE, .maps = holding_maps, .map_count = 6},
    {.area = AMODBUS_AREA_INPUT_REGISTERS, .quantity = 2,
     .flags = AMODBUS_ACCESS_READ, .maps = input_maps, .map_count = 1}
};

static aStatus_t sig_read(void *context,
    const aModbusSigReadRequest_t *request)
{
    assert(context == &bus);
    sig_reads++;
    if (request->target.paramIndex == AMODBUS_SIG_WHOLE) {
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
    const aModbusSigWriteRequest_t *request)
{
    assert(context == &bus);
    sig_writes++;
#if AMODBUS_SERVER_ENABLE
    if (callback_instance != NULL) {
        aModbusServerProcessRequest_t nested;
        aModbusServerProcessRequestStructInit(&nested);
        assert(aModbusServerProcess(callback_instance, &nested) ==
               A_STATUS_BUSY);
    }
#endif
    if (reject_sig_write) return A_STATUS_BUSY;
    if (request->target.paramIndex == AMODBUS_SIG_WHOLE) {
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
    aModbusHandle_t *server;
    aModbusTransportType_t type;
    aBool_t processed;
    aBool_t corrupt;
    aBool_t short_tcp;
    uint32_t delay;
    unsigned completed;
    unsigned prepared;
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
    assert(aModbusServerProcess(link->server, &request) == A_STATUS_OK);
    if (link->corrupt && link->response_size != 0U) {
        link->response[link->response_size - 1U] ^= 1U;
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
    if (!port_wait(link, timeout)) return aOSFailWithStatus(A_STATUS_TIMEOUT);
#if AMODBUS_SERVER_ENABLE
    if (!endpoint->server && !link->processed && link->request_size != 0U) {
        serve(link);
    }
#endif
    buffer = endpoint->server ? link->request : link->response;
    position = endpoint->server ? &link->request_pos : &link->response_pos;
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
    if (!port_wait(link, timeout)) return aOSFailWithStatus(A_STATUS_TIMEOUT);
    if (size > 3U) size = 3U;
    assert(*used + size <= sizeof(link->request));
    memcpy(buffer + *used, data, size);
    *used += size;
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
    endpoint->link->prepared++;
    return A_STATUS_OK;
}

static aStatus_t port_complete(void *context, aTimeout_t timeout)
{
    endpoint_t *endpoint = context;
    (void)timeout;
    endpoint->link->completed++;
#if AMODBUS_SERVER_ENABLE
    if (!endpoint->server &&
        endpoint->link->type == AMODBUS_TRANSPORT_RTU &&
        endpoint->link->request[0] == 0U) serve(endpoint->link);
#endif
    return A_STATUS_OK;
}

static aModbusConfig_t configuration(endpoint_t *endpoint,
                                     aModbusRole_t role)
{
    aModbusConfig_t config;
    aModbusConfigStructInit(&config);
    config.role = role;
    config.transport_type = endpoint->link->type;
    config.bus = &bus;
    config.transport.context = endpoint;
    config.transport.read = port_read;
    config.transport.write = port_write;
    config.transport.discard_input = port_discard;
    config.transport.prepare_frame = port_prepare;
    config.transport.wait_transmit_complete = port_complete;
    if (role == AMODBUS_ROLE_SERVER) {
        config.ranges = ranges;
        config.range_count = sizeof(ranges) / sizeof(ranges[0]);
    }
    return config;
}

static aModbusHandle_t *open_instance(const aModbusConfig_t *config,
                                     aModbusHandle_t *storage)
{
#if AMODBUS_STATIC_ENABLE
    assert(aModbusInitStatic(config, storage) == A_STATUS_OK);
    return storage;
#else
    aModbusHandle_t *handle;
    (void)storage;
    assert(aModbusCreate(config, &handle) == A_STATUS_OK);
    return handle;
#endif
}

static void close_instance(aModbusHandle_t *handle)
{
#if AMODBUS_STATIC_ENABLE
    assert(aModbusDeInitStatic(handle) == A_STATUS_OK);
#else
    assert(aModbusDestroy(handle) == A_STATUS_OK);
#endif
}

static aStatus_t invalid_config(const aModbusConfig_t *config)
{
#if AMODBUS_STATIC_ENABLE
    aModbusHandle_t storage;
    return aModbusInitStatic(config, &storage);
#else
    aModbusHandle_t *handle = (void *)1;
    aStatus_t status = aModbusCreate(config, &handle);
    assert(handle == NULL);
    return status;
#endif
}

static void lifecycle(void)
{
    link_t link = {.type = AMODBUS_TRANSPORT_RTU};
    endpoint_t endpoint = {&link, A_TRUE};
    aModbusHandle_t storage;
    aModbusConfig_t config = configuration(&endpoint,
#if AMODBUS_SERVER_ENABLE
        AMODBUS_ROLE_SERVER
#else
        AMODBUS_ROLE_CLIENT
#endif
    );
    aModbusHandle_t *handle = open_instance(&config, &storage);
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
    assert(aModbusCreate(&config, &handle) == A_STATUS_NO_MEMORY);
    assert(handle == NULL);
    testFailAllocation(A_FALSE);
    assert(aModbusCreate(&config, &handle) == A_STATUS_OK);
#if AMODBUS_STATIC_ENABLE
    assert(aModbusDeInitStatic(handle) == A_STATUS_INVALID_PARAM);
#endif
    assert(aModbusDestroy(handle) == A_STATUS_OK);
    assert(aModbusDestroy(NULL) == A_STATUS_OK);
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
static aStatus_t read_remote(aModbusHandle_t *client, aModbusArea_t area,
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
    return aModbusClientRead(client, &request);
}

static aStatus_t write_remote(aModbusHandle_t *client, aModbusArea_t area,
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
    return aModbusClientWrite(client, &request);
}

static void end_to_end(aModbusTransportType_t type, aBool_t callbacks)
{
    link_t link = {.type = type};
    endpoint_t server_port = {&link, A_TRUE};
    endpoint_t client_port = {&link, A_FALSE};
    aModbusConfig_t server_config =
        configuration(&server_port, AMODBUS_ROLE_SERVER);
    aModbusConfig_t client_config =
        configuration(&client_port, AMODBUS_ROLE_CLIENT);
    aModbusHandle_t server_storage;
    aModbusHandle_t client_storage;
    aModbusHandle_t *client;
    aModbusResult_t result;
    uint16_t registers[12];
    uint8_t bits[2] = {0xA5, 0x03};
    uint8_t read_bits[3] = {0, 0, 0xCC};
    aModbusClientSigRequest_t sig_request;

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
    registers[0] = 0xCAFE;
    registers[1] = 0xBABE;
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        1, 2, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(counter == 0xCAFEBABEU);
    assert(write_remote(client, AMODBUS_AREA_HOLDING_REGISTERS,
        2, 1, registers, sizeof(registers), &result) == A_STATUS_ERROR);
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
    aModbusClientSigRequestStructInit(&sig_request);
    sig_request.address = 1;
    sig_request.target.deviceID = 1;
    sig_request.target.sigIndex = LOCAL;
    assert(aModbusClientReadSig(client, &sig_request) == A_STATUS_OK);
    assert(local_value == counter);
    local_value = 12345;
    assert(aModbusClientWriteSig(client, &sig_request) == A_STATUS_OK);
    assert(counter == 12345);
    sig_request.address = 0;
    sig_request.area = AMODBUS_AREA_COILS;
    sig_request.target.sigIndex = 0;
    assert(aModbusClientReadSig(client, &sig_request) == A_STATUS_OK);
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
        assert(aModbusClientWrite(client, &broadcast) == A_STATUS_OK);
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
    static const aModbusAddressRange_t maximum_ranges[] = {
        {.area = AMODBUS_AREA_COILS, .quantity = 2000, .flags = READ_WRITE,
         .read = maximum_read, .write = maximum_write},
        {.area = AMODBUS_AREA_HOLDING_REGISTERS, .quantity = 65536,
         .flags = READ_WRITE, .read = maximum_read, .write = maximum_write}
    };
    link_t link = {.type = type};
    endpoint_t server_port = {&link, A_TRUE};
    endpoint_t client_port = {&link, A_FALSE};
    aModbusConfig_t server_config =
        configuration(&server_port, AMODBUS_ROLE_SERVER);
    aModbusConfig_t client_config =
        configuration(&client_port, AMODBUS_ROLE_CLIENT);
    aModbusHandle_t server_storage;
    aModbusHandle_t client_storage;
    aModbusHandle_t *client;
    uint16_t registers[125];
    uint8_t bits[251];
    aModbusResult_t result;
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
    aModbusConfig_t config = configuration(&port, AMODBUS_ROLE_SERVER);
    aModbusHandle_t storage;
    aModbusServerProcessRequest_t process;
    aModbusResult_t result;
    aModbusClientReadRequest_t read;
    endpoint_t client_port = {&link, A_FALSE};
    aModbusConfig_t client_config =
        configuration(&client_port, AMODBUS_ROLE_CLIENT);
    aModbusHandle_t client_storage;
    aModbusHandle_t *client;

    link.server = open_instance(&config, &storage);
    aModbusServerProcessRequestStructInit(&process);
    process.result = &result;
    assert(aModbusServerProcess(link.server, &process) == A_STATUS_OK);
    /* 声明 255 字节负载，验证接收前的帧缓冲边界保护。 */
    memset(link.request, 0, sizeof(link.request));
    link.request[0] = 1;
    link.request[1] = 16;
    link.request[5] = 123;
    link.request[6] = 255;
    link.request_size = 264;
    assert(aModbusServerProcess(link.server, &process) == A_STATUS_ERROR);
    link.request_size = 0;
    link.request_pos = 0;
    assert(aModbusServerProcess(link.server, &process) == A_STATUS_OK);
    client = open_instance(&client_config, &client_storage);
    aModbusClientReadRequestStructInit(&read);
    read.access.data = &word;
    read.access.size = sizeof(word);
    read.access.quantity = 1;
    read.access.timeout = A_TIMEOUT_MS(2U);
    link.delay = 1;
    testSetTime(UINT32_MAX - 1U);
    assert(aModbusClientRead(client, &read) == A_STATUS_TIMEOUT);
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
    assert(aModbusServerProcess(link.server, &process) == A_STATUS_ERROR);
    assert(aModbusServerProcess(link.server, &process) == A_STATUS_NOT_READY);
    close_instance(link.server);

    /* 地址段重叠和整个 STRUCT 映射在初始化时拒绝。 */
    aModbusAddressRange_t invalid_ranges[2] = {ranges[0], ranges[0]};
    config.ranges = invalid_ranges;
    config.range_count = 2;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    aModbusBusMap_t invalid_map = holding_maps[0];
    invalid_map.target.sigIndex = MOTOR;
    invalid_ranges[0] = ranges[3];
    invalid_ranges[0].maps = &invalid_map;
    invalid_ranges[0].map_count = 1;
    config.range_count = 1;
    assert(invalid_config(&config) == A_STATUS_UNSUPPORTED);
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
