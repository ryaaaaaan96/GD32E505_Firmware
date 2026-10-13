/* 在解锁点插入整组更新，验证协议不会拼接两代数据。 */
#define main original_modbus_main
#include "test_modbus.c"
#undef main

static fixtureEndpoints_t preflight;
static aModbusMappingConfig_t preflightConnection(const fixtureConfig_t *config)
{
    preflight.protocol = NULL;
    assert(aBusEndpointInit(&preflight.data, config->bus) == A_STATUS_OK);
    return fixtureConnection(config, &preflight);
}
#include "aModbus_mapping_internal.h"

static aBool_t inject_update;
static unsigned unlocks;
aStatus_t __real_aOSMutexUnlock(aOSMutex_t mutex);
aStatus_t __wrap_aOSMutexUnlock(aOSMutex_t mutex)
{
    aStatus_t status = __real_aOSMutexUnlock(mutex);
    unlocks++;
    if (inject_update) {
        motor_t next = {4000U, 35};
        const aBusSetIndexRequest_t set = {
            .deviceID = 1U, .sigIndex = MOTOR, .src = &next,
            .size = sizeof(next), .timeout = A_TIMEOUT_NO_WAIT
        };
        inject_update = A_FALSE;
        assert(aBusSetByIndex(&bus, &set) == A_STATUS_OK);
    }
    return status;
}

/* 模拟远端值与本地副本不同，确认合并帧解码后只提交一次 Motor。 */
static motor_t remote_motor = {4500U, 45};
static aStatus_t remote_read(void *context,
    const aCommValueReadRequest_t *request)
{
    (void)context;
    assert(request->target.sigIndex == MOTOR);
    if (request->target.paramIndex == ACOMM_MAP_SIG_WHOLE) {
        assert(request->size == sizeof(remote_motor));
        memcpy(request->data, &remote_motor, sizeof(remote_motor));
        return A_STATUS_OK;
    }
    memcpy(request->data, request->target.paramIndex == 0U ?
        (const void *)&remote_motor.speed : &remote_motor.temperature, 4U);
    return A_STATUS_OK;
}

static void plan_tests(void)
{
    static const aModbusServerMap_t maps[] = {
        {.area = AMODBUS_AREA_HOLDING_REGISTERS,
         .address = 0U, .quantity = 2U, .comm.flags = ACOMM_MAP_FLAG_READ,
         .comm.deviceID = 1U,
                   .comm.sigIndex = MOTOR,
                   .comm.paramIndex = 0U},
        {.area = AMODBUS_AREA_HOLDING_REGISTERS,
         .address = 2U, .quantity = 2U, .comm.flags = ACOMM_MAP_FLAG_READ,
         .comm.deviceID = 1U,
                   .comm.sigIndex = MOTOR,
                   .comm.paramIndex = 1U}
    };
    link_t link = {.type = AMODBUS_TRANSPORT_RTU};
    endpoint_t server_port = {&link, A_TRUE}, client_port = {&link, A_FALSE};
    fixtureConfig_t server = configuration(&server_port, AMODBUS_ROLE_SERVER);
    fixtureConfig_t client = configuration(&client_port, AMODBUS_ROLE_CLIENT);
    aModbusMapping_t server_storage, client_storage, *handle;
    aModbusClientMap_t items[2];
    aModbusResult_t result;
    size_t completed;
    fixturePlanConfig_t plan_config = {
        .items = items, .count = 2U,
        .unit_id = 1U, .timeout = A_TIMEOUT_MS(1000U)
    };

    server.ranges = NULL;
    server.range_count = 0U;
    server.maps = maps;
    server.map_count = 2U;
    server.sig_read = remote_read;
    link.server = open_instance(&server, &server_storage);
    handle = open_instance(&client, &client_storage);
    for (size_t i = 0U; i < 2U; i++) {
        items[i] = (aModbusClientMap_t){
            .comm.flags = ACOMM_MAP_FLAG_READ,
            .comm.poll_time_ms = ACOMM_POLL_ONCE,
            .area = AMODBUS_AREA_HOLDING_REGISTERS,
            .comm.deviceID = 1U,
                   .comm.sigIndex = MOTOR,
                   .comm.paramIndex = i,
            .address = (uint16_t)(i * 2U), .quantity = 2U
        };
    }
    /* 错误长度在发帧前拒绝，不能按目标类型自动纠正配置或发布数据。 */
    {
        const uint16_t invalid[] = {0U, 1U, 3U, 126U, UINT16_MAX};
        const unsigned writes = link.writes[0];
        const motor_t previous = motor;
        for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            items[0].quantity = invalid[i];
            assert(aModbusMappingClientMapCheck(handle->config, &items[0], 1U) ==
                   A_STATUS_INVALID_PARAM);
            assert(execute_first_plan(
                    handle, &plan_config, &completed, &result) ==
                   A_STATUS_INVALID_PARAM);
            assert(completed == 0U && link.writes[0] == writes);
        }
        items[0].quantity = 2U;
        items[1].quantity = 1U;
        assert(execute_first_plan(handle, &plan_config, &completed, &result) ==
               A_STATUS_INVALID_PARAM);
        assert(completed == 0U && link.writes[0] == writes);
        assert(motor.speed == previous.speed &&
               motor.temperature == previous.temperature);
        items[1].quantity = 2U;
    }
    unlocks = 0U;
    assert(execute_first_plan(
            handle, &plan_config, &completed, &result) == A_STATUS_OK);
    assert(completed == 2U && link.request[5] == 4U);
    assert(unlocks == 1U && motor.speed == 4500U && motor.temperature == 45);
    assert(result.exception == 0U);
    remote_motor = (motor_t){6001U, 46};
    unlocks = 0U;
    assert(execute_first_plan(
            handle, &plan_config, &completed, &result) == A_STATUS_INVALID_PARAM);
    assert(completed == 2U && unlocks == 0U);
    assert(motor.speed == 4500U && motor.temperature == 45);
    remote_motor.speed = 1234U;
    /* 空洞、区域和显式断帧标记必须停止当前批次。 */
    items[1].address = 3U;
    assert(execute_first_plan(
            handle, &plan_config, &completed, &result) == A_STATUS_OK);
    assert(completed == 1U && link.request[5] == 2U);
    items[1].address = 2U;
    items[1].comm.flags |= ACOMM_MAP_FLAG_BREAK_BEFORE;
    assert(execute_first_plan(
            handle, &plan_config, &completed, &result) == A_STATUS_OK);
    assert(completed == 1U && link.request[5] == 2U);
    /* 下一次从带标记的首项开始读取，不能发空帧或反复停在同一项。 */
    plan_config.items = &items[1];
    plan_config.count = 1U;
    remote_motor.temperature = 47;
    assert(execute_first_plan(
            handle, &plan_config, &completed, &result) == A_STATUS_OK);
    assert(completed == 1U && motor.temperature == 47);
    plan_config.items = items;
    plan_config.count = 2U;
    items[1].comm.flags &= ~ACOMM_MAP_FLAG_BREAK_BEFORE;
    items[1].area = AMODBUS_AREA_INPUT_REGISTERS;
    assert(execute_first_plan(
            handle, &plan_config, &completed, &result) == A_STATUS_OK);
    assert(completed == 1U);
    items[1].area = items[0].area;
    items[0].comm.flags = ACOMM_MAP_FLAG_READ | (1U << 15); /* 未定义的标志不能被当成自动合并。 */
    assert(aModbusMappingClientMapCheck(handle->config, &items[0], 1U) ==
           A_STATUS_INVALID_PARAM);
    assert(execute_first_plan(
            handle, &plan_config, &completed, &result) == A_STATUS_INVALID_PARAM);
    items[0].comm.flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_BREAK_BEFORE;
    assert(execute_first_plan(
            handle, &plan_config, &completed, &result) == A_STATUS_OK);
    assert(completed == 2U && link.request[5] == 4U);
    items[0].comm.flags = ACOMM_MAP_FLAG_READ;
    plan_config.unit_id = 0U; /* 读取禁止使用 RTU 广播。 */
    assert(execute_first_plan(
            handle, &plan_config, &completed, &result) == A_STATUS_INVALID_PARAM);
    plan_config.unit_id = 1U;
    plan_config.timeout.type = (aTimeoutType_t)-1;
    assert(execute_first_plan(
            handle, &plan_config, &completed, &result) == A_STATUS_INVALID_PARAM);
    assert(completed == 0U);
    plan_config.timeout = A_TIMEOUT_MS(500U);
    items[0].address = 4U;
    items[1].address = 6U;
    assert(execute_first_plan(
            handle, &plan_config, &completed, &result) != A_STATUS_OK);
    assert(completed == 2U && result.exception == 2U);
    close_instance(handle);
    close_instance(link.server);
}

static void whole_tests(void)
{
    const aModbusServerMap_t map = MAP(0U, 4U, MOTOR);
    link_t link = {.type = AMODBUS_TRANSPORT_RTU};
    endpoint_t server_port = {&link, A_TRUE}, client_port = {&link, A_FALSE};
    fixtureConfig_t server = configuration(&server_port, AMODBUS_ROLE_SERVER);
    fixtureConfig_t client = configuration(&client_port, AMODBUS_ROLE_CLIENT);
    aModbusMapping_t server_storage, client_storage, *handle;
    uint16_t registers[4] = {0U, 1234U, 0U, 15U};
    aModbusAddressReadRequest_t read = {
        .area = AMODBUS_AREA_HOLDING_REGISTERS, .quantity = 4U,
        .data = registers, .size = sizeof(registers),
        .timeout = A_TIMEOUT_MS(100U)
    };
    aModbusAddressWriteRequest_t write = {
        .area = AMODBUS_AREA_HOLDING_REGISTERS, .quantity = 4U,
        .data = registers, .size = sizeof(registers),
        .timeout = A_TIMEOUT_MS(100U)
    };
    const aModbusClientMap_t item = {
        .comm.flags = ACOMM_MAP_FLAG_READ,
            .comm.poll_time_ms = ACOMM_POLL_ONCE,
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 0U, .quantity = 4U,
        .comm.deviceID = 1U,
                   .comm.sigIndex = MOTOR,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE
    };
    size_t completed;
    const fixturePlanConfig_t plan_config = {
        .items = &item, .count = 1U,
        .unit_id = 1U, .timeout = A_TIMEOUT_MS(1000U)
    };
    server.maps = &map;
    server.map_count = 1U;
    server.ranges = NULL;
    server.range_count = 0U;
    handle = open_instance(&server, &server_storage);
    handle->deadline = aTimepointCalc(read.timeout, aOSGetUptimeMs());
    unlocks = 0U;
    assert(aModbusMappingDataWrite(handle, &write) == A_STATUS_OK);
    assert(unlocks == 1U && motor.speed == 1234U && motor.temperature == 15);
    inject_update = A_TRUE;
    assert(aModbusMappingDataRead(handle, &read) == A_STATUS_OK);
    assert(registers[1] == 1234U && registers[3] == 15U);
    assert(motor.speed == 4000U && motor.temperature == 35);
    /* 整组内只更新一个完整字段，其他字段保持更新后的值。 */
    write.quantity = 2U;
    registers[1] = 2222U;
    unlocks = 0U;
    assert(aModbusMappingDataWrite(handle, &write) == A_STATUS_OK);
    assert(unlocks == 1U && motor.speed == 2222U && motor.temperature == 35);
    close_instance(handle);

    server.sig_read = remote_read;
    remote_motor = (motor_t){4500U, 45};
    link.server = open_instance(&server, &server_storage);
    handle = open_instance(&client, &client_storage);
    unlocks = 0U;
    assert(execute_first_plan(
            handle, &plan_config, &completed, NULL) == A_STATUS_OK);
    assert(completed == 1U && unlocks == 1U);
    assert(motor.speed == 4500U && motor.temperature == 45);
    remote_motor = (motor_t){6001U, 99};
    unlocks = 0U;
    assert(execute_first_plan(
            handle, &plan_config, &completed, NULL) == A_STATUS_INVALID_PARAM);
    assert(unlocks == 0U && motor.speed == 4500U && motor.temperature == 45);
    close_instance(handle);
    close_instance(link.server);
}

int main(void)
{
    static const aModbusServerMap_t maps[] = {
        {.area = AMODBUS_AREA_HOLDING_REGISTERS,
         .address = 0U, .quantity = 2U,
         .comm.flags = READ_WRITE, .comm.deviceID = 1U,
                   .comm.sigIndex = MOTOR,
                   .comm.paramIndex = 0U},
        MAP(2U, 1U, WORD),
        {.area = AMODBUS_AREA_HOLDING_REGISTERS,
         .address = 3U, .quantity = 2U,
         .comm.flags = READ_WRITE, .comm.deviceID = 1U,
                   .comm.sigIndex = MOTOR,
                   .comm.paramIndex = 1U}
    };
    link_t link = {.type = AMODBUS_TRANSPORT_RTU};
    endpoint_t endpoint = {&link, A_TRUE};
    fixtureConfig_t config = configuration(&endpoint, AMODBUS_ROLE_SERVER);
    aModbusMapping_t storage, *handle;
    uint16_t registers[5];
    aModbusAddressReadRequest_t read = {
        .area = AMODBUS_AREA_HOLDING_REGISTERS, .quantity = 5U,
        .data = registers, .size = sizeof(registers),
        .timeout = A_TIMEOUT_MS(100U)
    };
    aModbusAddressWriteRequest_t write = {
        .area = AMODBUS_AREA_HOLDING_REGISTERS, .quantity = 5U,
        .data = registers, .size = sizeof(registers),
        .timeout = A_TIMEOUT_MS(100U)
    };
    aModbusMappingClientSigRequest_t poll;
    aModbusClientMap_t length_check = {
        .comm.flags = ACOMM_MAP_FLAG_READ,
            .comm.poll_time_ms = ACOMM_POLL_ONCE,
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 0U, .quantity = 3U,
        .comm.deviceID = 1U,
                   .comm.sigIndex = RAW,
                   .comm.paramIndex = ACOMM_MAP_SIG_WHOLE
    };

    aBusInstanceStructInit(&bus, states, SIG_COUNT + 1U);
    assert(aBusInitStatic(7U, tables, 2U, &bus) == A_STATUS_OK);
    config.ranges = NULL;
    config.range_count = 0U;
    config.maps = maps;
    config.map_count = 3U;
    handle = open_instance(&config, &storage);
    handle->deadline = aTimepointCalc(read.timeout, aOSGetUptimeMs());
    inject_update = A_TRUE;
    assert(aModbusMappingDataRead(handle, &read) == A_STATUS_OK);
    assert(registers[1] == 3000U && registers[4] == 25U);
    assert(motor.speed == 4000U && motor.temperature == 35);
    registers[0] = 0U; registers[1] = 5000U;
    registers[2] = 123U; registers[3] = 0U; registers[4] = 40U;
    unlocks = 0U;
    assert(aModbusMappingDataWrite(handle, &write) == A_STATUS_OK);
    assert(motor.speed == 5000U && motor.temperature == 40);
    assert(unlocks == 2U); /* 两个 SIG，各提交一次；Motor 的字段不连续。 */
    registers[1] = 6001U;
    assert(aModbusMappingDataWrite(handle, &write) == A_STATUS_INVALID_PARAM);
    assert(motor.speed == 5000U && motor.temperature == 40);
    close_instance(handle);

    aModbusMappingConfig_t connection = preflightConnection(&config);
    /* RAW[5] 需要三个寄存器，U8 位目标只占一位，长度单位不能混用。 */
    assert(aModbusMappingClientMapCheck(&connection, &length_check, 1U) ==
           A_STATUS_OK);
    length_check.quantity = 5U;
    assert(aModbusMappingClientMapCheck(&connection, &length_check, 1U) ==
           A_STATUS_INVALID_PARAM);
    length_check.area = AMODBUS_AREA_COILS;
    length_check.comm.sigIndex = 0U;
    length_check.quantity = 1U;
    assert(aModbusMappingClientMapCheck(&connection, &length_check, 1U) ==
           A_STATUS_OK);
    length_check.quantity = 8U;
    assert(aModbusMappingClientMapCheck(&connection, &length_check, 1U) ==
           A_STATUS_INVALID_PARAM);
    aModbusMappingClientSigRequestStructInit(&poll);
    poll.area = AMODBUS_AREA_HOLDING_REGISTERS;
    poll.target = (aCommTarget_t){1U, MOTOR, 0U};
    assert(aModbusMappingClientSigCheck(&connection, &poll, A_FALSE) == A_STATUS_OK);
    poll.target.deviceID = 99U;
    assert(aModbusMappingClientSigCheck(&connection, &poll, A_FALSE) ==
           A_STATUS_NOT_FOUND);
    poll.target.deviceID = 1U;
    poll.unit_id = 0U;
    aModbusHandle_t protocol = {0};
    const aModbusClientConfig_t core = {.modbus = {
        .transport_type = config.transport_type,
        .transport = config.transport, .byte_timeout = config.byte_timeout
    }};
    assert(aModbusClientInitStatic(&core, &protocol) == A_STATUS_OK);
    preflight.protocol = &protocol;
    assert(aModbusMappingClientSigCheck(&connection, &poll, A_FALSE) ==
           A_STATUS_INVALID_PARAM);
    assert(aModbusDeInitStatic(&protocol) == A_STATUS_OK);
    plan_tests();
    whole_tests();
    assert(aBusDeInitStatic(&bus) == A_STATUS_OK);
    assert(testAllocations() == 0U);
    puts("Protocol SIG snapshot/atomic update/preflight checks passed");
    return 0;
}
