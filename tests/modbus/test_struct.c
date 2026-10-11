/* 复用真实传输和 aBus 测试设施，覆盖整组布局及部分寄存器访问。 */
#define main original_modbus_main
#include "test_modbus.c"
#undef main
#include "aModbus_internal.h"

typedef struct {
    uint32_t number;
    int32_t signed_number;
    uint16_t word;
    uint8_t bytes[6];
    uint32_t tail;
} mixed_t;
ALIB_ASSERT_TYPE_SIZE(mixed_t, 20U);

static mixed_t mixed;
static const mixed_t initial = {
    1234U, -42, 999U, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, 0x89ABCDEFU
};
static const aBusRange_t signed_range = {.min.s32 = -1000, .max.s32 = 1000};
static const aBusParam_t mixed_params[] = {
    {.offset = offsetof(mixed_t, number), .size = 4U,
     .type = ALIB_DATA_U32, .range = &speed_range},
    {.offset = offsetof(mixed_t, signed_number), .size = 4U,
     .type = ALIB_DATA_S32, .range = &signed_range},
    {.offset = offsetof(mixed_t, word), .size = 2U,
     .type = ALIB_DATA_U16, .range = &word_range},
    {.offset = offsetof(mixed_t, bytes), .size = 6U, .type = ALIB_DATA_RAW},
    {.offset = offsetof(mixed_t, tail), .size = 4U, .type = ALIB_DATA_U32},
};
static const aBusSig_t mixed_sig = {
    .sigKey = 1U, .type = ALIB_DATA_STRUCT, .size = sizeof(mixed_t),
    .params = mixed_params, .param_count = 5U, .default_data = &initial,
    .flags = ABUS_SIG_FLAG_LOCK | ABUS_SIG_FLAG_DISJOINT
};
ABUS_RAM_BIND_EXPORT(mixed_binding, 7U, 3U, 0U, mixed);

static void invalid_layouts(void)
{
    aBusSig_t definition = mixed_sig;
    aBusParam_t fields[5];
    const aBusTable_t table = {
        .deviceID = 3U, .sigs = &definition, .sig_count = 1U
    };
    /* 只用于定义查询，无数据读写；确保检查不依赖 aBus 定义检查开关。 */
    aBusHandle_t metadata_bus = {.tables = &table, .table_count = 1U};
    aModbusBusMap_t map = {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 100U, .quantity = 10U, .flags = READ_WRITE,
        .target = {3U, 0U, AMODBUS_SIG_WHOLE}
    };
    link_t link = {.type = AMODBUS_TRANSPORT_RTU};
    endpoint_t port = {&link, A_TRUE};
    aModbusConfig_t config = configuration(&port, AMODBUS_ROLE_SERVER);
    aModbusHandle_t storage;

    config.bus = &metadata_bus;
    config.maps = &map;
    config.map_count = 1U;
    config.ranges = NULL;
    config.range_count = 0U;
    close_instance(open_instance(&config, &storage));
    for (unsigned scenario = 0U; scenario < 12U; scenario++) {
        definition = mixed_sig;
        memcpy(fields, mixed_params, sizeof(fields));
        definition.params = fields;
        switch (scenario) {
        case 0: fields[1].offset += 2U; break; /* 中间空隙。 */
        case 1: fields[1].offset = 0U; break; /* 重复长度抵消遗漏。 */
        case 2: fields[0] = mixed_params[1]; break; /* 顺序不匹配。 */
        case 3: fields[3].size = 5U; break; /* 奇数 RAW。 */
        case 4: fields[2].size = 1U; fields[2].type = ALIB_DATA_U8; break;
        case 5: definition.param_count--; break; /* 尾部未覆盖。 */
        case 6: fields[0].size = 0U; break;
        case 7: fields[0].size = SIZE_MAX - 1U; break;
        case 8: definition.params = NULL; break;
        case 9: definition.param_count = 0U; break;
        case 10: definition.size--; break;
        case 11: fields[3].range = &word_range; break;
        }
        assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    }
    definition = mixed_sig;
    memcpy(fields, mixed_params, sizeof(fields));
    definition.params = fields;
    fields[0].type = ALIB_DATA_STRUCT;
    assert(invalid_config(&config) == A_STATUS_UNSUPPORTED);
    definition = mixed_sig;
    map.area = AMODBUS_AREA_COILS;
    assert(invalid_config(&config) == A_STATUS_UNSUPPORTED);
    map.area = AMODBUS_AREA_HOLDING_REGISTERS;
    map.quantity = 9U;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    map.quantity = 10U;
    config.snapshot = &mixed;
    config.snapshot_size = sizeof(mixed) - 1U;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    assert(link.prepared == 0U);
}

static aStatus_t local_read(void *context,
    const aModbusSigReadRequest_t *request)
{
    assert(request->target.paramIndex == AMODBUS_SIG_WHOLE);
    assert(request->size == sizeof(mixed_t));
    memcpy(request->data, context, sizeof(mixed_t));
    return A_STATUS_OK;
}

static aStatus_t local_write(void *context,
    const aModbusSigWriteRequest_t *request)
{
    assert(request->target.paramIndex == AMODBUS_SIG_WHOLE);
    assert(request->size == sizeof(mixed_t));
    memcpy(context, request->data, sizeof(mixed_t));
    return A_STATUS_OK;
}

static void transfers(aModbusTransportType_t type, aBool_t callbacks)
{
    static const aModbusBusMap_t maps[] = {
        {.area = AMODBUS_AREA_HOLDING_REGISTERS,
         .address = 100U, .quantity = 10U, .flags = READ_WRITE,
         .target = {3U, 0U, AMODBUS_SIG_WHOLE}},
        MAP(110U, 1U, WORD),
        {.area = AMODBUS_AREA_HOLDING_REGISTERS,
         .address = 200U, .quantity = 10U, .flags = READ_WRITE,
         .target = {3U, 0U, AMODBUS_SIG_WHOLE},
         .word_order = AMODBUS_WORD_LOW_FIRST},
        {.area = AMODBUS_AREA_INPUT_REGISTERS,
         .address = 0U, .quantity = 10U, .flags = AMODBUS_ACCESS_READ,
         .target = {3U, 0U, AMODBUS_SIG_WHOLE}},
    };
    const uint16_t expected[] = {0U, 1234U, 0xFFFFU, 0xFFD6U, 999U,
                               0x1122U, 0x3344U, 0x5566U, 0x89ABU, 0xCDEFU};
    link_t link = {.type = type};
    endpoint_t server_port = {&link, A_TRUE}, client_port = {&link, A_FALSE};
    aModbusConfig_t server = configuration(&server_port, AMODBUS_ROLE_SERVER);
    aModbusConfig_t client = configuration(&client_port, AMODBUS_ROLE_CLIENT);
    aModbusHandle_t server_storage, client_storage, *handle;
    aModbusResult_t result;
    uint16_t registers[11];
    unsigned before;
    mixed_t local;
    aModbusClientSigRequest_t request = {
        .unit_id = 1U, .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 100U, .target = {3U, 0U, AMODBUS_SIG_WHOLE},
        .timeout = A_TIMEOUT_MS(1000U), .result = &result
    };

    mixed = initial;
    word = 123U;
    server.maps = maps;
    server.map_count = sizeof(maps) / sizeof(maps[0]);
    server.ranges = NULL;
    server.range_count = 0U;
    if (callbacks) {
        server.sig_read = sig_read;
        server.sig_write = sig_write;
        server.sig_context = &bus;
    }
    link.server = open_instance(&server, &server_storage);
    callback_instance = callbacks ? link.server : NULL;
    client.sig_read = local_read;
    client.sig_write = local_write;
    client.sig_context = &local;
    handle = open_instance(&client, &client_storage);

    assert(read_remote(handle, AMODBUS_AREA_HOLDING_REGISTERS,
        100U, 10U, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(memcmp(registers, expected, sizeof(expected)) == 0);
    assert(read_remote(handle, AMODBUS_AREA_INPUT_REGISTERS,
        0U, 10U, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(memcmp(registers, expected, sizeof(expected)) == 0);
    /* 穷举所有合法子区间，包括半个 U32、RAW 片段、跨字段。 */
    for (uint16_t first = 0U; first < 10U; first++) {
        for (uint16_t count = 1U; count <= 10U - first; count++) {
            assert(read_remote(handle, AMODBUS_AREA_HOLDING_REGISTERS,
                (uint16_t)(100U + first), count, registers,
                sizeof(registers), &result) == A_STATUS_OK);
            assert(memcmp(registers, expected + first, count * 2U) == 0);
        }
    }
    assert(read_remote(handle, AMODBUS_AREA_HOLDING_REGISTERS,
        200U, 10U, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(registers[0] == 1234U && registers[1] == 0U);
    assert(registers[2] == 0xFFD6U && registers[3] == 0xFFFFU);
    assert(registers[4] == 999U && registers[5] == 0x1122U);
    assert(registers[8] == 0xCDEFU && registers[9] == 0x89ABU);
    assert(write_remote(handle, AMODBUS_AREA_HOLDING_REGISTERS,
        200U, 10U, registers, sizeof(registers), &result) == A_STATUS_OK);
    assert(memcmp(&mixed, &initial, sizeof(mixed)) == 0);

    before = sig_writes;
    assert(write_remote(handle, AMODBUS_AREA_HOLDING_REGISTERS,
        101U, 1U, expected, 2U, &result) == A_STATUS_ERROR);
    assert(result.exception == 2U);
    assert(write_remote(handle, AMODBUS_AREA_HOLDING_REGISTERS,
        105U, 1U, expected, 2U, &result) == A_STATUS_ERROR);
    assert(result.exception == 2U && sig_writes == before);
    assert(memcmp(&mixed, &initial, sizeof(mixed)) == 0);

    registers[0] = 0xFFFFU; registers[1] = (uint16_t)-500;
    registers[2] = 456U;
    assert(write_remote(handle, AMODBUS_AREA_HOLDING_REGISTERS,
        102U, 3U, registers, 6U, &result) == A_STATUS_OK);
    assert(mixed.signed_number == -500 && mixed.word == 456U);
    assert(mixed.number == initial.number && mixed.tail == initial.tail);
    if (callbacks) assert(sig_writes == before + 2U);

    registers[0] = 0U; registers[1] = 6001U;
    registers[2] = 0U; registers[3] = 1U;
    before = sig_writes;
    assert(write_remote(handle, AMODBUS_AREA_HOLDING_REGISTERS,
        100U, 4U, registers, 8U, &result) == A_STATUS_ERROR);
    assert(result.exception == 3U && sig_writes == before);
    assert(mixed.number == initial.number && mixed.signed_number == -500);

    /* 后续映射范围错误时，前面的整组字段也不能被提交。 */
    registers[0] = 0U; registers[1] = 88U; registers[2] = 1001U;
    assert(write_remote(handle, AMODBUS_AREA_HOLDING_REGISTERS,
        108U, 3U, registers, 6U, &result) == A_STATUS_ERROR);
    assert(result.exception == 3U && mixed.tail == initial.tail);
    registers[2] = 321U;
    assert(write_remote(handle, AMODBUS_AREA_HOLDING_REGISTERS,
        108U, 3U, registers, 6U, &result) == A_STATUS_OK);
    assert(mixed.tail == 88U && word == 321U);

    assert(aModbusClientReadSig(handle, &request) == A_STATUS_OK);
    assert(memcmp(&local, &mixed, sizeof(local)) == 0);
    local = initial;
    before = sig_writes;
    assert(aModbusClientWriteSig(handle, &request) == A_STATUS_OK);
    assert(memcmp(&mixed, &initial, sizeof(mixed)) == 0);
    if (callbacks) assert(sig_writes == before + 1U);
    {
        aModbusPoll_t poll = {
            .area = request.area, .address = 100U, .quantity = 10U,
            .flags = 0U, .target = request.target
        };
        size_t completed;
        const aModbusClientBatchRequest_t batch = {
            .items = &poll, .count = 1U, .completed = &completed,
            .unit_id = 1U, .timeout = A_TIMEOUT_MS(1000U)
        };
        memset(&local, 0, sizeof(local));
        assert(aModbusClientReadBatch(handle, &batch) == A_STATUS_OK);
        assert(completed == 1U && memcmp(&local, &mixed, sizeof(local)) == 0);
        poll.quantity = 9U;
        before = link.writes[0];
        assert(aModbusClientReadBatch(handle, &batch) ==
               A_STATUS_INVALID_PARAM);
        assert(link.writes[0] == before);
    }
    close_instance(handle);
    close_instance(link.server);
    callback_instance = NULL;
}

int main(void)
{
    const aBusTable_t all_tables[] = {
        tables[0], tables[1],
        {.deviceID = 3U, .sigs = &mixed_sig, .sig_count = 1U}
    };
    aBusSigState_t all_states[SIG_COUNT + 2U];
    aBusInstanceStructInit(&bus, all_states, SIG_COUNT + 2U);
    assert(aBusInitStatic(7U, all_tables, 3U, &bus) == A_STATUS_OK);
    invalid_layouts();
    transfers(AMODBUS_TRANSPORT_RTU, A_FALSE);
    transfers(AMODBUS_TRANSPORT_RTU, A_TRUE);
    transfers(AMODBUS_TRANSPORT_TCP, A_FALSE);
    transfers(AMODBUS_TRANSPORT_TCP, A_TRUE);
    assert(aBusDeInitStatic(&bus) == A_STATUS_OK);
    assert(testAllocations() == 0U);
    puts("STRUCT: layout, full/partial RTU/TCP, callbacks and client passed");
    return 0;
}
