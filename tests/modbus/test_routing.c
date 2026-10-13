/* 使用真实实例和 aBus，验证平级回调段/离散映射的路由边界。 */
#define main original_modbus_main
#include "test_modbus.c"
#undef main
#include "aModbus_mapping_internal.h"

typedef struct {
    uint16_t values[2];
    unsigned reads;
    unsigned writes;
    aStatus_t result;
} rangeContext_t;

static aStatus_t range_read(void *context,
    const aModbusAddressReadRequest_t *request)
{
    rangeContext_t *state = context;
    size_t offset = request->address - 10U;
    assert(request->area == AMODBUS_AREA_HOLDING_REGISTERS);
    assert(offset < 2U && request->quantity <= 2U - offset);
    state->reads++;
    if (state->result != A_STATUS_OK) return state->result;
    memcpy(request->data, state->values + offset,
           request->quantity * sizeof(uint16_t));
    return A_STATUS_OK;
}

static aStatus_t range_write(void *context,
    const aModbusAddressWriteRequest_t *request)
{
    rangeContext_t *state = context;
    size_t offset = request->address - 10U;
    assert(request->area == AMODBUS_AREA_HOLDING_REGISTERS);
    assert(offset < 2U && request->quantity <= 2U - offset);
    state->writes++;
    if (state->result != A_STATUS_OK) return state->result;
    memcpy(state->values + offset, request->data,
           request->quantity * sizeof(uint16_t));
    return A_STATUS_OK;
}

static void invalid_lengths(const fixtureConfig_t *base)
{
    const aModbusServerMap_t valid[] = {
        MAP(0U, 1U, WORD),
        MAP(0U, 2U, COUNTER),
        MAP(0U, 2U, SIGNED),
        MAP(0U, 3U, RAW), /* RAW[5] 是三个寄存器，不是五个。 */
        {.area = AMODBUS_AREA_COILS,
         .address = 0U, .quantity = 1U, .comm.flags = READ_WRITE,
         .comm.deviceID = 1U,
                   .comm.sigIndex = 0U,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE},
        {.area = AMODBUS_AREA_HOLDING_REGISTERS,
         .address = 0U, .quantity = 2U, .comm.flags = READ_WRITE,
         .comm.deviceID = 1U,
                   .comm.sigIndex = MOTOR,
                   .comm.paramIndex = 0U}
    };
    fixtureConfig_t config = *base;
    aModbusServerMap_t map;
    aModbusMapping_t storage;
    link_t *link = ((endpoint_t *)config.transport.context)->link;
    unsigned allocations = testAllocations();
    unsigned prepared = link->prepared;

    config.ranges = NULL;
    config.range_count = 0U;
    config.maps = &map;
    config.map_count = 1U;
    for (size_t i = 0U; i < sizeof(valid) / sizeof(valid[0]); i++) {
        map = valid[i];
        close_instance(open_instance(&config, &storage));
        map.quantity = 0U; /* 缺省值不能退回自动推导长度。 */
        assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
        map.quantity = (uint16_t)(valid[i].quantity + 1U);
        assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
        if (valid[i].quantity > 1U) {
            map.quantity = (uint16_t)(valid[i].quantity - 1U);
            assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
        }
        map.quantity = UINT16_MAX;
        assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    }
    /* 初始化辅助函数要求调用者填写长度，不提供隐式的一个寄存器。 */
    aModbusMappingServerMapStructInit(&map);
    map.comm.deviceID = 1U;
    map.comm.sigIndex = WORD;
    map.comm.paramIndex = ACOMM_MAP_SIG_WHOLE;
    assert(map.quantity == 0U);
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    assert(testAllocations() == allocations);
    assert(link->prepared == prepared);
}

static void invalid_routes(const fixtureConfig_t *base)
{
    fixtureConfig_t config = *base;
    aModbusServerMap_t bad_maps[2] = {base->maps[0], base->maps[1]};
    aModbusMappingAddressRange_t bad_ranges[2] = {base->ranges[0], base->ranges[0]};

    config.ranges = NULL;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    config.range_count = 0U;
    config.maps = NULL;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    config.map_count = 0U;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    config.maps = base->maps;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    config = *base;
    config.range_count = 0U;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);

    config = *base;
    config.ranges = bad_ranges;
    config.range_count = 2U;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    bad_ranges[1].address = 9U; /* 段未排序。 */
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    config.range_count = 1U;
    bad_ranges[0].read = NULL;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    bad_ranges[0] = base->ranges[0];
    bad_ranges[0].quantity = UINT32_MAX;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    bad_ranges[0].quantity = 0U;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);

    config = *base;
    config.maps = bad_maps;
    config.map_count = 2U;
    bad_maps[1].address = 9U; /* 起点不同，仍与前一 U32 的低字重叠。 */
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    bad_maps[0] = base->maps[1];
    bad_maps[1] = base->maps[0]; /* 映射未排序。 */
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);

    config.map_count = 1U;
    bad_maps[0] = base->maps[0];
    bad_maps[0].address = 9U; /* U32 跨入回调段。 */
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    bad_maps[0].address = 10U; /* 映射与回调段完全重合。 */
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    bad_maps[0].address = 11U; /* 从回调段内部延伸到外部。 */
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    bad_maps[0].address = UINT16_MAX; /* 完整宽度超出地址空间。 */
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    bad_maps[0] = base->maps[0];
    config.ranges = bad_ranges;
    bad_ranges[0] = base->ranges[0];
    bad_ranges[0].address = 9U;
    bad_ranges[0].quantity = 1U; /* 回调段落在 U32 映射内部。 */
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);

    config.ranges = base->ranges;
    bad_maps[0] = base->maps[2];
    bad_maps[0].comm.flags = READ_WRITE; /* 输入区域不允许写。 */
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
    bad_maps[0].area = (aModbusArea_t)99;
    assert(invalid_config(&config) == A_STATUS_INVALID_PARAM);
}

int main(void)
{
    rangeContext_t context = {.values = {100U, 200U}};
    const aModbusMappingAddressRange_t callbacks[] = {
        {.area = AMODBUS_AREA_HOLDING_REGISTERS, .address = 10U,
         .quantity = 2U, .flags = READ_WRITE,
         .read = range_read, .write = range_write, .context = &context}
    };
    const aModbusServerMap_t targets[] = {
        MAP(8U, 2U, COUNTER), MAP(12U, 1U, WORD),
        {.area = AMODBUS_AREA_INPUT_REGISTERS, .address = 8U,
         .quantity = 2U, .comm.flags = ACOMM_MAP_FLAG_READ,
         .comm.deviceID = 1U,
                   .comm.sigIndex = COUNTER,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE}
    };
    link_t link = {.type = AMODBUS_TRANSPORT_RTU};
    endpoint_t endpoint = {&link, A_TRUE};
    aModbusMapping_t storage, *handle;
    fixtureConfig_t config = configuration(&endpoint, AMODBUS_ROLE_SERVER);
    uint16_t values[6];
    aModbusAddressReadRequest_t read = {
        .area = AMODBUS_AREA_HOLDING_REGISTERS, .address = 8U, .quantity = 5U,
        .data = values, .size = sizeof(values), .timeout = A_TIMEOUT_MS(100U)
    };
    aModbusAddressWriteRequest_t write = {
        .area = AMODBUS_AREA_HOLDING_REGISTERS, .address = 8U, .quantity = 5U,
        .data = values, .size = sizeof(values), .timeout = A_TIMEOUT_MS(100U)
    };
    unsigned reads, writes;

    aBusInstanceStructInit(&bus, states, SIG_COUNT + 1U);
    assert(aBusInitStatic(7U, tables, 2U, &bus) == A_STATUS_OK);
    config.ranges = callbacks;
    config.range_count = 1U;
    config.maps = targets;
    config.map_count = 3U;
    config.sig_read = sig_read;
    config.sig_write = sig_write;
    config.sig_context = &bus;
    invalid_lengths(&config);
    invalid_routes(&config);
    handle = open_instance(&config, &storage);
    handle->deadline = aTimepointCalc(read.timeout, aOSGetUptimeMs());

    /* 一次请求经过映射、回调、映射，回调只接收自己负责的部分。 */
    assert(aModbusMappingDataRead(handle, &read) == A_STATUS_OK);
    assert(memcmp(values, (uint16_t[]){0x1234, 0x5678, 100, 200, 123},
                  5U * sizeof(uint16_t)) == 0);
    assert(context.reads == 1U && sig_reads == 2U);
    memcpy(values, (uint16_t[]){0x1111, 0x2222, 333, 444, 555},
           5U * sizeof(uint16_t));
    assert(aModbusMappingDataWrite(handle, &write) == A_STATUS_OK);
    assert(counter == 0x11112222U && word == 555U);
    assert(context.writes == 1U && sig_writes == 2U);
    assert(context.values[0] == 333U && context.values[1] == 444U);

    /* 最后一格空洞导致整帧预检失败，前面的业务回调不会执行。 */
    read.quantity = write.quantity = 6U;
    reads = context.reads; writes = context.writes;
    sig_reads = sig_writes = 0U;
    memset(values, 0xA5, sizeof(values));
    assert(aModbusMappingDataRead(handle, &read) == A_STATUS_NOT_FOUND);
    assert(aModbusMappingDataWrite(handle, &write) == A_STATUS_NOT_FOUND);
    assert(context.reads == reads && context.writes == writes);
    assert(sig_reads == 0U && sig_writes == 0U && values[0] == 0xA5A5U);

    /* 回调拒绝访问或失败时不继续执行后面的离散映射。 */
    read.address = write.address = 10U;
    read.quantity = write.quantity = 3U;
    const aStatus_t failures[] = {
        A_STATUS_NOT_FOUND, A_STATUS_BUSY, A_STATUS_TIMEOUT
    };
    for (size_t i = 0U; i < sizeof(failures) / sizeof(failures[0]); i++) {
        context.result = failures[i];
        assert(aModbusMappingDataRead(handle, &read) == failures[i]);
        values[2] = 321U;
        assert(aModbusMappingDataWrite(handle, &write) == failures[i]);
        assert(sig_reads == 0U && sig_writes == 0U && word == 555U);
    }
    context.result = A_STATUS_OK;
    read.address = 11U;
    read.quantity = 1U;
    assert(aModbusMappingDataRead(handle, &read) == A_STATUS_OK && values[0] == 444U);
    read.area = AMODBUS_AREA_INPUT_REGISTERS;
    read.address = 8U;
    read.quantity = 2U;
    assert(aModbusMappingDataRead(handle, &read) == A_STATUS_OK);
    assert(values[0] == 0x1111U && values[1] == 0x2222U);
    close_instance(handle);

    /* 同一地址段只读时，写入在调用业务前被拒绝。 */
    aModbusMappingAddressRange_t readonly = callbacks[0];
    readonly.flags = ACOMM_MAP_FLAG_READ;
    readonly.write = NULL;
    config.ranges = &readonly;
    handle = open_instance(&config, &storage);
    writes = context.writes;
    write.quantity = 1U;
    assert(aModbusMappingDataWrite(handle, &write) == A_STATUS_NOT_FOUND);
    assert(context.writes == writes);
    close_instance(handle);

    /* 最后一个寄存器可独立映射；不强制存在任何地址段。 */
    aModbusServerMap_t last = MAP(UINT16_MAX, 1U, WORD);
    config.ranges = NULL;
    config.range_count = 0U;
    config.maps = &last;
    config.map_count = 1U;
    handle = open_instance(&config, &storage);
    handle->deadline = aTimepointCalc(read.timeout, aOSGetUptimeMs());
    read.area = AMODBUS_AREA_HOLDING_REGISTERS;
    read.address = UINT16_MAX;
    read.quantity = 1U;
    assert(aModbusMappingDataRead(handle, &read) == A_STATUS_OK && values[0] == word);
    close_instance(handle);
    assert(aBusDeInitStatic(&bus) == A_STATUS_OK);
    assert(testAllocations() == 0U);
    puts("Flat routing/callback failure/overlap/sparse map checks passed");
    return 0;
}
