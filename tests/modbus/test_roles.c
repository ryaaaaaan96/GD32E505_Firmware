/* 映射适配可使用其他执行器：这里不链接通用调度，用单槽队列执行请求。 */
#define main original_modbus_main
#include "test_modbus.c"
#undef main

static fixtureEndpoints_t preflight;
static aCommMappingConfig_t preflightConnection(const fixtureConfig_t *config)
{
    aModbusEndpointInit(&preflight.protocol, NULL);
    assert(aBusEndpointInit(&preflight.data, config->bus) == A_STATUS_OK);
    return fixtureConnection(config, &preflight);
}

static const aCommRequest_t *queued;
static aBool_t queue_full;
static unsigned notifications;

static aStatus_t submit(void *context, const aCommRequest_t *work)
{
    assert(context == &queued);
    if (queue_full || queued != NULL) return A_STATUS_BUSY;
    queued = work;
    return A_STATUS_OK;
}

static void notified(void *context, const aCommMappingServerMap_t *map,
    const aCommNotifyInput_t *input)
{
    assert(context == &notifications);
    assert(map != NULL);
    assert(input->event == ABUS_EVENT_UPDATED);
    assert(input->delivery == NULL);
    notifications++;
}

static aStatus_t release(aCommMapping_t *handle)
{
#if AMODBUS_DYNAMIC_ENABLE
    return fixtureDestroy(handle);
#else
    return fixtureDeInitStatic(handle);
#endif
}

static void roles(void)
{
    link_t wire = {.type = AMODBUS_TRANSPORT_RTU};
    endpoint_t server_port = {&wire, A_TRUE}, client_port = {&wire, A_FALSE};
    fixtureConfig_t server_fixture = configuration(
        &server_port, AMODBUS_ROLE_SERVER);
    aCommMappingServerMap_t map = MAP(0U, 2U, COUNTER);
    map.target.subscribe_events = ABUS_EVENT_UPDATED;
    aCommMappingServerConfig_t server;
    aCommMappingServerConfigStructInit(&server);
    server.binding = preflightConnection(&server_fixture);
    server_fixture.server_override = &server;
    server.maps = &map;
    server.map_count = 1U;
    server.notify = notified;
    server.notify_context = &notifications;

    aCommMappingClientMap_t client_map = {
        .area = AMODBUS_AREA_HOLDING_REGISTERS, .address = 0U,
        .quantity = 2U, .access = ACOMM_MAP_ACCESS_READ | ACOMM_MAP_ACCESS_WRITE,
        .target = {.deviceID = 1U, .sigIndex = COUNTER,
            .paramIndex = ABUS_SIG_WHOLE,
            .subscribe_events = ABUS_REQUEST_READ | ABUS_REQUEST_WRITE},
        .poll_time_ms = 0U
    };
    aCommMappingClientConfig_t client;
    aCommMappingClientConfigStructInit(&client);
    fixtureConfig_t client_fixture = configuration(
        &client_port, AMODBUS_ROLE_CLIENT);
    client.binding = preflightConnection(&client_fixture);
    client_fixture.client_override = &client;
    client.maps = &client_map;
    client.map_count = 1U;
    client.submit = submit;
    client.submit_context = &queued;
    aCommMapping_t *master;
#if AMODBUS_DYNAMIC_ENABLE
    assert(fixtureCreate(&server_fixture, &wire.server) == A_STATUS_OK);
    assert(fixtureCreate(&client_fixture, &master) == A_STATUS_OK);
#else
    aCommMappingSubscription_t server_subs[1], client_subs[1];
    aCommMapping_t server_storage = {
        .subscriptions = server_subs, .subscription_capacity = 1U
    };
    aCommMapping_t client_storage = {
        .subscriptions = client_subs, .subscription_capacity = 1U
    };
    assert(fixtureOpen(&server_fixture, &server_storage, NULL, A_FALSE) == A_STATUS_OK);
    assert(fixtureOpen(&client_fixture, &client_storage, NULL, A_FALSE) == A_STATUS_OK);
    assert(fixtureOpen(&client_fixture, &client_storage, NULL, A_FALSE) == A_STATUS_BUSY);
    wire.server = &server_storage;
    master = &client_storage;
#endif
    /* 从站绑定拒绝主动请求；主站只有事件项，完全不创建计划或 aComm。 */
    assert(master->plan_count == 0U);
    assert(master->subscription_count == 1U);
    assert(wire.server->subscription_count == 1U);
    uint32_t value = 234U;
    aBusRequestConfig_t request_config = {
        .target = {1U, COUNTER, ABUS_SIG_WHOLE},
        .event = ABUS_REQUEST_WRITE,
        .data = &value, .size = sizeof(value)
    };
    aBusDelivery_t delivery[1];
    aBusRequest_t request;
    aBusRequestStructInit(&request, delivery, 1U);
    aBusDispatchResult_t dispatched;
    queue_full = A_TRUE;
    assert(aBusRequest(&bus, &request_config, &request, &dispatched) ==
           A_STATUS_BUSY);
    assert(dispatched.accepted == 0U && aBusRequestDone(&request));
    assert(queued == NULL && notifications == 0U);
    queue_full = A_FALSE;
    assert(aBusRequest(&bus, &request_config, &request, &dispatched) ==
           A_STATUS_OK);
    assert(dispatched.accepted == 1U && !aBusRequestDone(&request));
    assert(release(master) == A_STATUS_BUSY);
    const aCommRequest_t *work = queued;
    queued = NULL;
    aStatus_t status = work->execute(work->context);
    assert(status == A_STATUS_OK && counter == value);
    work->complete(work->context, status);
    assert(aBusRequestDone(&request));
    assert(notifications == 1U); /* 收到远端写入，仅由 aBus 发布一次。 */
    assert(release(wire.server) == A_STATUS_OK);

    /* 仅登记关系可以不提供任何回调；从站主动读写请求必须明确拒绝。 */
    server.notify = NULL;
    map.target.subscribe_events = ABUS_SUBSCRIBE_MARK_ONLY;
    assert(aCommMappingServerConfigCheck(&server) == A_STATUS_OK);
#if AMODBUS_DYNAMIC_ENABLE
    assert(fixtureCreate(&server_fixture, &wire.server) == A_STATUS_OK);
#else
    assert(fixtureOpen(&server_fixture, &server_storage, NULL, A_FALSE) == A_STATUS_OK);
    wire.server = &server_storage;
#endif
    const aBusAssociationQuery_t query = {
        .target = {1U, COUNTER, ABUS_SIG_WHOLE},
        .type = ABUS_PROVIDER_MODBUS, .owner = wire.server
    };
    aBusSubscriberConfig_t association;
    assert(aBusAssociationGet(&bus, &query, &association) == A_STATUS_OK);
    assert(association.definition == &map);
    const aBusPublishRequest_t update = {
        .target = {1U, COUNTER, ABUS_SIG_WHOLE}, .event = ABUS_EVENT_UPDATED
    };
    assert(aBusPublish(&bus, &update, &dispatched) == A_STATUS_OK);
    assert(dispatched.matched == 0U && notifications == 1U);
    assert(release(wire.server) == A_STATUS_OK);
    map.target.subscribe_events = ABUS_REQUEST_READ;
    assert(aCommMappingServerConfigCheck(&server) == A_STATUS_UNSUPPORTED);
    map.target.subscribe_events = ABUS_EVENT_UPDATED;
    assert(aCommMappingServerConfigCheck(&server) == A_STATUS_INVALID_PARAM);
    client.binding.protocol = master->config->protocol;
    client.submit = NULL;
    assert(aCommMappingClientConfigCheck(&client) == A_STATUS_INVALID_PARAM);
    client_map.target.subscribe_events = ABUS_SUBSCRIBE_MARK_ONLY;
    assert(aCommMappingClientConfigCheck(&client) == A_STATUS_OK);
    assert(release(master) == A_STATUS_OK);
}

static uint8_t reply[12];
static size_t reply_size, reply_at;
static aModbusTransportType_t malformed_type;

static aSSize_t malformed_read(void *context, void *data, size_t size,
    aTimeout_t timeout)
{
    (void)context;
    (void)timeout;
    size_t left = reply_size - reply_at;
    if (left == 0U) return aOSFailWithStatus(A_STATUS_TIMEOUT);
    if (size > left) size = left;
    memcpy(data, reply + reply_at, size);
    reply_at += size;
    return (aSSize_t)size;
}

static aSSize_t malformed_write(void *context, const void *data, size_t size,
    aTimeout_t timeout)
{
    (void)context;
    (void)timeout;
    reply_at = 0U;
    if (malformed_type == AMODBUS_TRANSPORT_TCP) {
        memcpy(reply, data, 2U); /* 保留当前事务号，错误仅在有效载荷长度。 */
    }
    return (aSSize_t)size;
}

static aStatus_t noop(void *context, aTimeout_t timeout)
{
    (void)context;
    (void)timeout;
    return A_STATUS_OK;
}

static void short_bits(aModbusTransportType_t type)
{
    fixtureConfig_t config;
    fixtureConfigInit(&config);
    config.role = AMODBUS_ROLE_CLIENT;
    config.bus = &bus;
    config.transport_type = type;
    config.transport.stream.read = malformed_read;
    config.transport.stream.write = malformed_write;
    config.transport.discard_input = noop;
    config.transport.prepare_frame = noop;
    config.transport.wait_transmit_complete = noop;
    malformed_type = type;
    memset(reply, 0, sizeof(reply));
    if (type == AMODBUS_TRANSPORT_RTU) {
        reply[0] = 1U; reply[1] = 1U; reply[2] = 1U; reply[3] = 0x55U;
        uint16_t crc = nmbs_crc_calc(reply, 4U, NULL);
        reply[4] = (uint8_t)(crc >> 8U); reply[5] = (uint8_t)crc;
        reply_size = 6U;
    } else {
        reply[5] = 4U; reply[6] = 1U; reply[7] = 1U;
        reply[8] = 1U; reply[9] = 0x55U;
        reply_size = 10U;
    }
    aCommMapping_t *handle;
#if AMODBUS_DYNAMIC_ENABLE
    assert(fixtureCreate(&config, &handle) == A_STATUS_OK);
#else
    aCommMapping_t storage = {0};
    assert(fixtureInitStatic(&config, &storage) == A_STATUS_OK);
    handle = &storage;
#endif
    uint8_t result[2] = {0xAAU, 0xAAU};
    aModbusClientReadRequest_t read;
    aModbusClientReadRequestStructInit(&read);
    read.access.area = AMODBUS_AREA_COILS;
    read.access.quantity = 9U;
    read.access.data = result;
    read.access.size = sizeof(result);
    assert(aModbusClientRead(fixtureProtocol(handle), &read) == A_STATUS_ERROR);
    assert(result[0] == 0xAAU && result[1] == 0xAAU);
    if (type == AMODBUS_TRANSPORT_RTU) {
        aCommMappingClientMap_t items[9];
        for (size_t i = 0U; i < 9U; i++) {
            items[i] = (aCommMappingClientMap_t){
                .area = AMODBUS_AREA_COILS, .address = (uint16_t)i,
                .quantity = 1U, .access = ACOMM_MAP_ACCESS_READ,
                .poll_time_ms = 1000U,
                .target = {.deviceID = 1U, .sigIndex = i,
                    .paramIndex = ABUS_SIG_WHOLE}
            };
        }
        const aCommPlanConfig_t plan_config = {
            .items = items, .count = 9U, .unit_id = 1U,
            .timeout = A_TIMEOUT_MS(1000U)
        };
        aCommPlan_t *plan;
#if AMODBUS_DYNAMIC_ENABLE
        assert(aCommPlanCreate(handle, &plan_config, &plan) == A_STATUS_OK);
#else
        aCommPlanFrame_t frames[1];
        size_t indices[9];
        aCommPlan_t plan_storage = {
            .frames = frames, .capacity = 1U,
            .indices = indices, .index_capacity = 9U
        };
        assert(aCommPlanInitStatic(handle, &plan_config, &plan_storage) ==
               A_STATUS_OK);
        plan = &plan_storage;
#endif
        uint8_t previous[sizeof(flags)];
        memcpy(previous, flags, sizeof(previous));
        const aCommPlanRequest_t execute = {.frame_index = 0U};
        assert(aCommPlanExecute(plan, &execute) == A_STATUS_ERROR);
        assert(memcmp(previous, flags, sizeof(previous)) == 0);
#if AMODBUS_DYNAMIC_ENABLE
        assert(aCommPlanDestroy(plan) == A_STATUS_OK);
#else
        assert(aCommPlanDeInitStatic(plan) == A_STATUS_OK);
#endif
    }
    assert(release(handle) == A_STATUS_OK);
}

int main(void)
{
    aBusParamState_t param_states[2];
    aBusInstanceStructInit(&bus, states, SIG_COUNT + 1U);
    bus.params = param_states;
    bus.param_capacity = 2U;
    assert(aBusInitStatic(7, tables, 2, &bus) == A_STATUS_OK);
    roles();
    short_bits(AMODBUS_TRANSPORT_RTU);
    short_bits(AMODBUS_TRANSPORT_TCP);
    assert(aBusDeInitStatic(&bus) == A_STATUS_OK);
    assert(testAllocations() == 0U);
    puts("Role APIs/events without scheduler and short bit responses passed");
    return 0;
}
