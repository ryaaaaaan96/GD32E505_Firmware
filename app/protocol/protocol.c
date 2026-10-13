#include "protocol.h"
#include "app_config.h"

#if APP_MODBUS_ENABLE
#include "protocol_internal.h"
#include "aBus_endpoint.h"
#include "rs485_device.h"
#if !ACOMM_ENABLE
#error "Protocol demo requires ACOMM_ENABLE"
#endif
#if !ACOMM_DYNAMIC_ENABLE
#include "aComm_instance.h"
#include "aComm_channel_instance.h"
#include "aComm_mapping_instance.h"
#endif
#if !AMODBUS_DYNAMIC_ENABLE
#include "aModbus_instance.h"
#endif
#if ASHELL_ENABLE
#include "aShell.h"
#endif

/* 只读配置在设备 .c 中定义，按当前角色声明并装配。 */
#if APP_MODBUS_CLIENT_ENABLE
#if !AMODBUS_CLIENT_ENABLE
#error "Modbus master demo requires AMODBUS_CLIENT_ENABLE"
#endif
extern const protocolClientConfig_t FAN_modbus_master_config;
typedef protocolClientConfig_t protocolProduct_t;
#else
#if !AMODBUS_SERVER_ENABLE
#error "Modbus slave demo requires AMODBUS_SERVER_ENABLE"
#endif
extern const protocolServerConfig_t IDU_modbus_slave_config;
typedef protocolServerConfig_t protocolProduct_t;
#endif

/* 每条通信链路独立持有状态；多个链路可借用同一个 aBus。
 * 当前硬件只注册 USART2，通道收发由 aComm 管理，协议通过回调接入。
 */
typedef struct {
    const protocolProduct_t *product;
    aOSTaskConfig_t task; /* 本链路的任务资源，入口和参数在创建时填充。 */
    aStatus_t (*prepare)(rs485Port_t *port);
    aStatus_t (*open)(rs485ReceiveFn_t receive, void *context);
    aStatus_t (*close)(void);
} protocolLinkConfig_t;

#if APP_MODBUS_CLIENT_ENABLE
typedef struct {
    aCommPlan_t *plan;
    size_t first_frame;
} protocolGroup_t;
#endif

/* 静态资源上限只在应用编排处维护，产品映射文件保持只读。 */
#if !ACOMM_DYNAMIC_ENABLE && APP_MODBUS_CLIENT_ENABLE
#define PROTOCOL_MAP_CAPACITY 50U
#endif

typedef struct {
    const protocolLinkConfig_t *config;
    aBusHandle_t *bus;
    aModbusHandle_t *modbus;
    aModbusEndpoint_t protocol_endpoint;
    aBusEndpoint_t data_endpoint;
    aCommMapping_t *mapping;
    aCommChannel_t *channel;
    aModbusRtuFilter_t filter;
    aCommHandle_t *comm;
#if APP_MODBUS_CLIENT_ENABLE
    aCommPlan_t *plan;
    aCommJobConfig_t *jobs;
    protocolGroup_t *groups;
    size_t group_count;
    aStatus_t previous_status;
#else
    aCommJobConfig_t server_job;
#endif
#if !AMODBUS_DYNAMIC_ENABLE
    aModbusHandle_t modbus_instance;
#endif
#if !ACOMM_DYNAMIC_ENABLE
    aCommMapping_t mapping_instance;
#if ABUS_EVENT_ENABLE
#if APP_MODBUS_CLIENT_ENABLE
    aCommMappingSubscription_t subscriptions[PROTOCOL_MAP_CAPACITY];
    aBusEndpointSubscription_t nodes[PROTOCOL_MAP_CAPACITY];
#else
    aCommMappingSubscription_t subscriptions[1];
    aBusEndpointSubscription_t nodes[1];
#endif
#endif
#if APP_MODBUS_CLIENT_ENABLE
    aCommPlan_t plan_instance;
    aCommPlanFrame_t frames[PROTOCOL_MAP_CAPACITY];
    size_t indices[PROTOCOL_MAP_CAPACITY];
#endif
#endif
#if !ACOMM_DYNAMIC_ENABLE
    aCommChannel_t channel_instance;
    aCommHandle_t comm_instance;
#if APP_MODBUS_CLIENT_ENABLE
    aCommJobConfig_t job_configs[PROTOCOL_MAP_CAPACITY];
    protocolGroup_t group_states[PROTOCOL_MAP_CAPACITY];
    aCommJobState_t job_states[PROTOCOL_MAP_CAPACITY];
    aCommRequest_t requests[4];
#else
    aCommJobState_t server_state;
#endif
#endif
} protocolLink_t;

static const protocolLinkConfig_t rs485_config;
static protocolLink_t rs485_link = {.config = &rs485_config};
static aStatus_t modbusDeInit(protocolLink_t *link);

/* 每条硬件链路注册自己的配置；总线无需知道协议或端口。 */
static const protocolLinkConfig_t rs485_config = {
#if APP_MODBUS_CLIENT_ENABLE
    .product = &FAN_modbus_master_config,
#else
    .product = &IDU_modbus_slave_config,
#endif
    .task = {
        .name = "modbus",
        .stack_bytes = 4096U,
        .priority = AOS_TASK_PRIO_NORMAL,
    },
    .prepare = rs485PortPrepare,
    .open = rs485PortOpen,
    .close = rs485PortClose,
};

#if APP_MODBUS_CLIENT_ENABLE
static void scheduledExecute(void *context, size_t step,
    aCommStepResult_t *result)
{
    protocolGroup_t *group = context;
    const aCommPlanRequest_t request = {
        .frame_index = group->first_frame + step,
        .execution = result
    };
    result->status = aCommPlanExecute(group->plan, &request);
}

#if ABUS_EVENT_ENABLE
static aStatus_t requestedSubmit(void *context, const aCommRequest_t *work)
{
    protocolLink_t *link = context;
    if (link->comm == NULL) return A_STATUS_NOT_READY;
    return aCommSubmit(link->comm, work);
}
#endif

/* 应用决定队列容量和任务节奏；协议仅提供分帧结果及帧执行入口。 */
static aStatus_t commInit(protocolLink_t *link)
{
    const protocolClientConfig_t *product = link->config->product;
    const aCommPlanConfig_t plan = {
        .items = product->client.maps, .count = product->client.map_count,
        .unit_id = product->client.unit_id,
        .timeout = product->client.request_timeout
    };
    aStatus_t status;
#if ACOMM_DYNAMIC_ENABLE
    status = aCommPlanCreate(link->mapping, &plan, &link->plan);
#else
    link->plan_instance = (aCommPlan_t){
        .indices = link->indices, .index_capacity = PROTOCOL_MAP_CAPACITY,
        .frames = link->frames, .capacity = PROTOCOL_MAP_CAPACITY
    };
    status = aCommPlanInitStatic(
        link->mapping, &plan, &link->plan_instance);
    if (status == A_STATUS_OK) link->plan = &link->plan_instance;
#endif
    if (status != A_STATUS_OK) return status;
    link->group_count = aCommPlanGroupCount(link->plan);
#if ACOMM_DYNAMIC_ENABLE
    if (link->group_count != 0U) {
        size_t size = sizeof(*link->jobs) + sizeof(*link->groups);
        if (link->group_count > SIZE_MAX / size) return A_STATUS_INVALID_PARAM;
        link->jobs = aOSAlloc(link->group_count * size);
        if (link->jobs == NULL) return A_STATUS_NO_MEMORY;
        link->groups = (void *)(link->jobs + link->group_count);
    }
#else
    if (link->group_count > PROTOCOL_MAP_CAPACITY) return A_STATUS_NO_MEMORY;
    link->jobs = link->job_configs;
    link->groups = link->group_states;
#endif
    for (size_t i = 0U; i < link->group_count; i++) {
        aCommPlanGroup_t group;
        status = aCommPlanGroupGet(link->plan, i, &group);
        if (status != A_STATUS_OK) return status;
        link->groups[i] = (protocolGroup_t){link->plan, group.first_frame};
        link->jobs[i] = (aCommJobConfig_t){
            .execute = scheduledExecute, .context = &link->groups[i],
            .step_count = group.frame_count, .poll_time_ms = group.poll_time_ms,
            .retry_count = product->retry_count,
            .interval_ms = product->interval_ms,
            .error_delay_ms = product->error_delay_ms
        };
    }
    const aCommConfig_t config = {
        .jobs = link->jobs, .job_count = link->group_count,
        .request_capacity = 4U
    };
#if ACOMM_DYNAMIC_ENABLE
    status = aCommCreate(&config, &link->comm);
#else
    link->comm_instance = (aCommHandle_t){
        .jobs = link->job_states, .job_capacity = PROTOCOL_MAP_CAPACITY,
        .requests = link->requests, .request_capacity = 4U
    };
    status = aCommInitStatic(&config, &link->comm_instance);
    if (status == A_STATUS_OK) link->comm = &link->comm_instance;
#endif
    link->previous_status = A_STATUS_NOT_READY;
    return status;
}
#else
static void serverExecute(void *context, size_t step,
    aCommStepResult_t *result)
{
    protocolLink_t *link = context;
    (void)step;
    result->status = aModbusServerProcess(link->modbus,
        &link->config->product->process);
    result->advance = A_TRUE;
    result->retryable = A_FALSE;
}

static aStatus_t commInit(protocolLink_t *link)
{
    aCommJobConfigStructInit(&link->server_job);
    link->server_job.execute = serverExecute;
    link->server_job.context = link;
    link->server_job.error_delay_ms = link->config->product->error_delay_ms;
    const aCommConfig_t config = {
        .jobs = &link->server_job, .job_count = 1U
    };
#if ACOMM_DYNAMIC_ENABLE
    return aCommCreate(&config, &link->comm);
#else
    link->comm_instance = (aCommHandle_t){
        .jobs = &link->server_state, .job_capacity = 1U
    };
    aStatus_t status = aCommInitStatic(&config, &link->comm_instance);
    if (status == A_STATUS_OK) link->comm = &link->comm_instance;
    return status;
#endif
}
#endif

static void modbusTask(void *argument)
{
    protocolLink_t *link = argument;
    for (;;) {
        aCommProcessResult_t result;
        aStatus_t status = aCommProcess(link->comm, &result);
        if (status != A_STATUS_OK) {
            aOSDelayMs(link->config->product->error_delay_ms);
            continue;
        }
#if ASHELL_ENABLE && APP_MODBUS_CLIENT_ENABLE
        if (result.type == ACOMM_WORK_JOB &&
            result.execution.status != link->previous_status) {
            ASHELL_PRINT("Modbus master: %s (status %d)\r\n",
                result.execution.status == A_STATUS_OK ? "online" :
                                                        "read failed",
                (int)result.execution.status);
            link->previous_status = result.execution.status;
        }
#endif
        status = aCommWait(link->comm, result.wait);
        if (status != A_STATUS_OK && status != A_STATUS_TIMEOUT &&
            status != A_STATUS_BUSY) {
            aOSDelayMs(link->config->product->error_delay_ms);
        }

    }
}

static aStatus_t modbusPrepare(protocolLink_t *link, aBusHandle_t *bus)
{
    rs485Port_t port;
    aCommChannelConfig_t framing;
#if APP_MODBUS_CLIENT_ENABLE
    aModbusClientConfig_t config = link->config->product->protocol;
    aCommMappingClientConfig_t mapping = link->config->product->client;
#else
    aModbusServerConfig_t config = link->config->product->protocol;
    aCommMappingServerConfig_t mapping = link->config->product->server;
#endif
    aStatus_t status;

    status = link->config->prepare(&port);
    if (status != A_STATUS_OK) return status;
    aCommChannelConfigStructInit(&framing);
    framing.mode = ACOMM_CHANNEL_GAP;
    framing.stream = port.output;
    /* 产品指定微秒值；aComm 负责计数换算、间隔判断与等待。 */
    framing.timing = link->config->product->timing;
#if APP_MODBUS_CLIENT_ENABLE
    link->filter = (aModbusRtuFilter_t){
        .role = AMODBUS_ROLE_CLIENT, .unit_id = mapping.unit_id
    };
#else
    link->filter = (aModbusRtuFilter_t){
        .role = AMODBUS_ROLE_SERVER, .unit_id = config.unit_id
    };
#endif
    framing.frame_check = aModbusRtuFrameCheck;
    framing.frame_context = &link->filter;
    framing.io.ticks_per_second = port.ticks_per_second;
    framing.io.ticks = port.ticks;
    framing.io.enter = port.enter;
    framing.io.exit = port.exit;
    framing.io.wait_transmit_complete = port.wait_transmit_complete;
    framing.io.clear_error = port.clear_error;
#if ACOMM_DYNAMIC_ENABLE
    status = aCommChannelCreate(&framing, &link->channel);
#else
    status = aCommChannelInitStatic(&framing, &link->channel_instance);
    if (status == A_STATUS_OK) link->channel = &link->channel_instance;
#endif
    if (status != A_STATUS_OK) return status;

    status = aBusEndpointInit(&link->data_endpoint, bus);
    if (status != A_STATUS_OK) return status;
    aModbusEndpointInit(&link->protocol_endpoint, NULL);
    mapping.binding.data = &link->data_endpoint.base;
    mapping.binding.protocol = &link->protocol_endpoint.base;
    aModbusTransportStructInit(&config.modbus.transport);
    status = aModbusBindChannel(link->channel, &config.modbus.transport);
    if (status != A_STATUS_OK) return status;
#if !ACOMM_DYNAMIC_ENABLE && ABUS_EVENT_ENABLE
    for (size_t i = 0U; i < sizeof(link->subscriptions) /
         sizeof(link->subscriptions[0]); i++) {
        link->subscriptions[i].native = &link->nodes[i];
    }
    link->mapping_instance.subscriptions = link->subscriptions;
    link->mapping_instance.subscription_capacity =
        sizeof(link->subscriptions) / sizeof(link->subscriptions[0]);
#endif
#if APP_MODBUS_CLIENT_ENABLE
#if AMODBUS_DYNAMIC_ENABLE
    status = aModbusClientCreate(&config, &link->modbus);
#else
    status = aModbusClientInitStatic(&config, &link->modbus_instance);
    if (status == A_STATUS_OK) link->modbus = &link->modbus_instance;
#endif
    if (status != A_STATUS_OK) return status;
    link->protocol_endpoint.protocol = link->modbus;
#if ABUS_EVENT_ENABLE
    mapping.submit = requestedSubmit;
    mapping.submit_context = link;
#endif
#if ACOMM_DYNAMIC_ENABLE
    status = aCommMappingClientCreate(&mapping, &link->mapping);
#else
    status = aCommMappingClientInitStatic(&mapping, &link->mapping_instance);
    if (status == A_STATUS_OK) link->mapping = &link->mapping_instance;
#endif
    if (status != A_STATUS_OK) return status;
#else
#if ACOMM_DYNAMIC_ENABLE
    status = aCommMappingServerCreate(&mapping, &link->mapping);
#else
    status = aCommMappingServerInitStatic(&mapping, &link->mapping_instance);
    if (status == A_STATUS_OK) link->mapping = &link->mapping_instance;
#endif
    if (status != A_STATUS_OK) return status;
    link->protocol_endpoint.read = aCommMappingServerRead;
    link->protocol_endpoint.write = aCommMappingServerWrite;
    link->protocol_endpoint.context = link->mapping;
    config.read = aModbusEndpointServerRead;
    config.write = aModbusEndpointServerWrite;
    config.context = &link->protocol_endpoint;
#if AMODBUS_DYNAMIC_ENABLE
    status = aModbusServerCreate(&config, &link->modbus);
#else
    status = aModbusServerInitStatic(&config, &link->modbus_instance);
    if (status == A_STATUS_OK) link->modbus = &link->modbus_instance;
#endif
#endif
    if (status != A_STATUS_OK) return status;
    status = commInit(link);
    if (status != A_STATUS_OK) return status;
    /* 注册和调度就绪后才开启生产者；ISR 只向通用通道交付字节。 */
    return link->config->open(aCommChannelReceive, link->channel);
}

static aStatus_t modbusInit(protocolLink_t *link, aBusHandle_t *bus)
{
    aOSTaskConfig_t task;
    aStatus_t status;
    aStatus_t cleanup_status;

    if (link->config == NULL || link->config->product == NULL ||
        link->config->prepare == NULL || link->config->open == NULL ||
        link->config->close == NULL) return A_STATUS_INVALID_PARAM;
    if (link->modbus != NULL || link->mapping != NULL ||
        link->channel != NULL) return A_STATUS_BUSY;
    link->bus = bus;
    if (bus == NULL) return A_STATUS_INVALID_PARAM;
    status = aBusEndpointInit(&link->data_endpoint, bus);
    if (status != A_STATUS_OK) return status;
    aModbusEndpointInit(&link->protocol_endpoint, NULL);
    const protocolProduct_t *product = link->config->product;
    const aModbusConfig_t *config = &product->protocol.modbus;
    if (config->transport_type != AMODBUS_TRANSPORT_RTU) {
        return A_STATUS_UNSUPPORTED;
    }
    if (!aTimeoutIsValid(config->byte_timeout) ||
        link->config->task.name == NULL ||
        link->config->task.priority < AOS_TASK_PRIO_LOWEST ||
        link->config->task.priority > AOS_TASK_PRIO_REALTIME ||
        product->error_delay_ms == 0U || product->error_delay_ms > INT32_MAX) {
        return A_STATUS_INVALID_PARAM;
    }
#if APP_MODBUS_CLIENT_ENABLE
    aCommMappingConfig_t check = product->client.binding;
    check.data = &link->data_endpoint.base;
    check.protocol = &link->protocol_endpoint.base;
    if (product->client.maps == NULL || product->client.map_count == 0U ||
        !aTimeoutIsValid(product->client.request_timeout) ||
        product->interval_ms > INT32_MAX) return A_STATUS_INVALID_PARAM;
    for (size_t i = 0U; i < product->client.map_count; i++) {
        status = aCommMappingClientMapCheck(&check, &product->client.maps[i],
            product->client.unit_id);
        if (status != A_STATUS_OK) return status;
    }
#else
    aCommMappingServerConfig_t check = product->server;
    check.binding.data = &link->data_endpoint.base;
    check.binding.protocol = &link->protocol_endpoint.base;
    status = aCommMappingServerConfigCheck(&check);
    if (status != A_STATUS_OK) return status;
    if (!aTimeoutIsValid(product->process.timeout) ||
        (product->process.timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
         product->process.timeout.milliseconds == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
#endif
    status = modbusPrepare(link, bus);
    if (status == A_STATUS_OK) {
        task = link->config->task;
        task.function = modbusTask;
        task.argument = link;
        status = aOSCreateTask(&task, NULL);
    }
    if (status != A_STATUS_OK) {
        /* 没有协议资源时不触碰可能被其他使用方占用的物理端口。 */
        if (link->modbus == NULL && link->mapping == NULL &&
            link->channel == NULL) return status;
        cleanup_status = modbusDeInit(link);
        return cleanup_status == A_STATUS_OK ? status : cleanup_status;
    }
#if ASHELL_ENABLE
#if APP_MODBUS_CLIENT_ENABLE
    ASHELL_PRINT("Modbus RTU master: unit %u\r\n",
        (unsigned)product->client.unit_id);
#else
    ASHELL_PRINT("Modbus RTU slave: unit %u\r\n",
        (unsigned)product->protocol.unit_id);
#endif
#endif
    return A_STATUS_OK;
}

static aStatus_t modbusDeInit(protocolLink_t *link)
{
    aStatus_t status;

    if (link->modbus == NULL && link->mapping == NULL &&
        link->channel == NULL) return A_STATUS_NOT_READY;
    if (link->comm != NULL) {
#if ACOMM_DYNAMIC_ENABLE
        status = aCommDestroy(link->comm);
#else
        status = aCommDeInitStatic(link->comm);
#endif
        if (status != A_STATUS_OK) return status;
        link->comm = NULL;
    }
#if APP_MODBUS_CLIENT_ENABLE
#if ACOMM_DYNAMIC_ENABLE
    aOSFree(link->jobs);
#endif
    link->jobs = NULL;
    link->groups = NULL;
    link->group_count = 0U;
    if (link->plan != NULL) {
#if ACOMM_DYNAMIC_ENABLE
        status = aCommPlanDestroy(link->plan);
#else
        status = aCommPlanDeInitStatic(link->plan);
#endif
        if (status != A_STATUS_OK) return status;
        link->plan = NULL;
    }
#endif
    if (link->mapping != NULL) {
#if ACOMM_DYNAMIC_ENABLE
        status = aCommMappingDestroy(link->mapping);
#else
        status = aCommMappingDeInitStatic(link->mapping);
#endif
        if (status != A_STATUS_OK) return status;
        link->mapping = NULL;
    }
    if (link->modbus != NULL) {
#if AMODBUS_DYNAMIC_ENABLE
        status = aModbusDestroy(link->modbus);
#else
        status = aModbusDeInitStatic(link->modbus);
#endif
        if (status != A_STATUS_OK) return status;
        link->modbus = NULL;
    }
    status = link->config->close();
    if (status != A_STATUS_OK && status != A_STATUS_NOT_READY) return status;
    /* Close 成功后 ISR 不再使用通道，才能释放接收状态。 */
    if (link->channel != NULL) {
#if ACOMM_DYNAMIC_ENABLE
        status = aCommChannelDestroy(link->channel);
#else
        status = aCommChannelDeInitStatic(link->channel);
#endif
        if (status != A_STATUS_OK) return status;
        link->channel = NULL;
    }
    return A_STATUS_OK;
}
#endif

/* 仅启动阶段使用；协议借用总线，失败后禁止重复创建。 */
aStatus_t protocolInit(aBusHandle_t *bus)
{
    static aBool_t initialization_started;

    if (initialization_started) return A_STATUS_BUSY;
    initialization_started = A_TRUE;
#if APP_MODBUS_ENABLE
    if (bus == NULL) return A_STATUS_INVALID_PARAM;
    protocolLink_t *const links[] = {&rs485_link};
    for (size_t i = 0U; i < sizeof(links) / sizeof(links[0]); i++) {
        aStatus_t status = modbusInit(links[i], bus);
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
#else
    (void)bus;
    return A_STATUS_OK;
#endif
}

/* 只投递事件，回调在通信任务中触发下一轮；不在调用方直接操作串口。 */
aStatus_t protocolRefresh(void)
{
#if APP_MODBUS_ENABLE && APP_MODBUS_CLIENT_ENABLE
    if (rs485_link.comm == NULL) return A_STATUS_NOT_READY;
    for (size_t i = 0U; i < rs485_link.group_count; i++) {
        aStatus_t status = aCommJobTrigger(rs485_link.comm, i);
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
#else
    return A_STATUS_UNSUPPORTED;
#endif
}

#if APP_MODBUS_ENABLE && APP_MODBUS_CLIENT_ENABLE
static aStatus_t temporaryExecute(void *context)
{
    const protocolModbusRequest_t *request = context;
    aModbusResult_t response = {.exception = 0U};
    aStatus_t status;
    if (request->write) {
        aModbusClientWriteRequest_t access = request->access.write;
        access.result = &response;
        status = aModbusClientWrite(rs485_link.modbus, &access);
    } else {
        aModbusClientReadRequest_t access = request->access.read;
        access.result = &response;
        status = aModbusClientRead(rs485_link.modbus, &access);
    }
    if (request->complete != NULL) {
        request->complete(request->context, status, &response);
    }
    return status;
}
#endif

#if APP_MODBUS_ENABLE
void protocolModbusRequestStructInit(protocolModbusRequest_t *request)
{
    if (request != NULL) {
        const protocolModbusRequest_t defaults = {.write = A_FALSE};
        *request = defaults;
        aModbusClientReadRequestStructInit(&request->access.read);
    }
}

aStatus_t protocolModbusSubmit(const protocolModbusRequest_t *request)
{
#if APP_MODBUS_CLIENT_ENABLE
    aCommRequest_t queued;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    if (rs485_link.comm == NULL) return A_STATUS_NOT_READY;
    aCommRequestStructInit(&queued);
    queued.execute = temporaryExecute;
    queued.context = (void *)request;
    return aCommSubmit(rs485_link.comm, &queued);
#else
    (void)request;
    return A_STATUS_UNSUPPORTED;
#endif
}
#endif

#if ABUS_EVENT_ENABLE && APP_MODBUS_ENABLE
aStatus_t protocolRequest(const aBusRequestConfig_t *config,
    aBusRequest_t *request, aBusDispatchResult_t *result)
{
    if (result != NULL) *result = (aBusDispatchResult_t){0};
    if (rs485_link.bus == NULL) return A_STATUS_NOT_READY;
    return aBusRequest(rs485_link.bus, config, request, result);
}
#endif
