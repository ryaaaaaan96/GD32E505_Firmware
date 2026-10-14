#include "protocol.h"
#include "app_config.h"

#if APP_MODBUS_ENABLE
#include "aModbus_mapping.h"
#include "aBus_endpoint.h"
#include "aAtomic.h"
#include "rs485_device.h"
#include "protocol_generated.h"
#if !ACOMM_ENABLE
#error "Protocol demo requires ACOMM_ENABLE"
#endif
#if !ACOMM_DYNAMIC_ENABLE
#include "aComm_instance.h"
#endif
#if !AMODBUS_DYNAMIC_ENABLE
#include "aModbus_instance.h"
#include "aModbus_mapping_instance.h"
#endif
#if ASHELL_ENABLE
#include "aShell.h"
#endif

/* 只读配置在设备 .c 中定义，按当前角色声明并装配。 */
#if APP_MODBUS_CLIENT_ENABLE
#if !AMODBUS_CLIENT_ENABLE
#error "Modbus master demo requires AMODBUS_CLIENT_ENABLE"
#endif
extern const aCommConfig_t FAN_modbus_master_config;
#else
#if !AMODBUS_SERVER_ENABLE
#error "Modbus slave demo requires AMODBUS_SERVER_ENABLE"
#endif
extern const aCommConfig_t IDU_modbus_slave_config;
#endif

/* 每条通信链路独立持有状态；多个链路可借用同一个 aBus。
 * 当前硬件只注册 USART2，通道收发由 aComm 管理，协议通过回调接入。
 */
typedef struct {
    const aCommConfig_t *product;
    aCommTiming_t timing;
    aOSTaskConfig_t task; /* 本链路的任务资源，入口和参数在创建时填充。 */
    aStatus_t (*prepare)(rs485Port_t *port);
    aStatus_t (*open)(rs485ReceiveFn_t receive, void *context);
    aStatus_t (*close)(void);
} protocolLinkConfig_t;

/* 静态资源上限只在应用编排处维护，产品映射文件保持只读。 */
#if !ACOMM_DYNAMIC_ENABLE || !AMODBUS_DYNAMIC_ENABLE
#if APP_MODBUS_CLIENT_ENABLE
#define PROTOCOL_MAP_CAPACITY FAN_MODBUS_MASTER_MAP_COUNT
#else
#define PROTOCOL_MAP_CAPACITY IDU_MODBUS_SLAVE_MAP_COUNT
#endif
#endif

typedef struct {
    const protocolLinkConfig_t *config;
    aBusHandle_t *bus;
    aModbusComm_t protocol;
    aBusEndpoint_t data_endpoint;
    aCommChannel_t *channel;
    aModbusRtuFilter_t filter;
    aCommHandle_t *comm;
    aAtomicUint_t work_count, error_count, timeout_count, max_process_ms;
#if APP_MODBUS_CLIENT_ENABLE
    aStatus_t previous_status;

#endif
#if !AMODBUS_DYNAMIC_ENABLE
    aModbusHandle_t modbus_instance;
    aModbusMapping_t mapping_instance;
    aModbusMapLink_t map_links[PROTOCOL_MAP_CAPACITY];
#if ABUS_EVENT_ENABLE
#if APP_MODBUS_CLIENT_ENABLE
    aCommBindingSubscription_t subscriptions[PROTOCOL_MAP_CAPACITY];
    aCommBindingRequestSlot_t binding_requests[4];
    aBusEndpointSubscription_t nodes[PROTOCOL_MAP_CAPACITY];
#else
    aCommBindingSubscription_t subscriptions[1];
    aBusEndpointSubscription_t nodes[1];
#endif
#endif
#endif
#if !ACOMM_DYNAMIC_ENABLE
#if APP_MODBUS_CLIENT_ENABLE
    aCommPlanFrame_t frames[PROTOCOL_MAP_CAPACITY];
    size_t indices[PROTOCOL_MAP_CAPACITY];
#endif
    aCommChannel_t channel_instance;
    aCommChannelStorage_t channel_storage;
    aCommHandle_t comm_instance;
#if APP_MODBUS_CLIENT_ENABLE
    aCommTableJob_t table_jobs[PROTOCOL_MAP_CAPACITY];
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
    /* 按实际串口参数计算默认微秒值，应用仍可替换为自定义间隔。 */
    .timing = {
        .character_us = AMODBUS_RTU_CHARACTER_US(
            APP_RS485_BAUD_RATE, APP_RS485_CHARACTER_BITS),
        .byte_gap_us = AMODBUS_RTU_BYTE_GAP_US(
            APP_RS485_BAUD_RATE, APP_RS485_CHARACTER_BITS),
        .frame_gap_us = AMODBUS_RTU_FRAME_GAP_US(
            APP_RS485_BAUD_RATE, APP_RS485_CHARACTER_BITS),
    },
    .task = {
        .name = "modbus",
        .stack_bytes = 4096U,
        .priority = AOS_TASK_PRIO_NORMAL,
    },
    .prepare = rs485PortPrepare,
    .open = rs485PortOpen,
    .close = rs485PortClose,
};

#if APP_MODBUS_CLIENT_ENABLE && ABUS_EVENT_ENABLE
static aStatus_t requestedSubmit(void *context, const aCommRequest_t *work)
{
    protocolLink_t *link = context;
    if (link->comm == NULL) return A_STATUS_NOT_READY;
    return aCommSubmit(link->comm, work);
}
#endif

/* 主从使用相同的注册入口；具体实例及映射由协议回调初始化。 */
static aStatus_t commInit(protocolLink_t *link, const aCommConfig_t *config)
{
    aStatus_t status;
#if ACOMM_DYNAMIC_ENABLE
    status = aCommCreate(config, &link->comm);
#else
    link->comm_instance = (aCommHandle_t){
#if APP_MODBUS_CLIENT_ENABLE
        .plan = {
            .frames = link->frames, .capacity = PROTOCOL_MAP_CAPACITY,
            .indices = link->indices, .index_capacity = PROTOCOL_MAP_CAPACITY
        },
        .table_jobs = link->table_jobs, .table_capacity = PROTOCOL_MAP_CAPACITY,
        .jobs = link->job_states, .job_capacity = PROTOCOL_MAP_CAPACITY,
        .requests = link->requests, .request_capacity = 4U
#else
        .jobs = &link->server_state, .job_capacity = 1U
#endif
    };
    status = aCommInitStatic(config, &link->comm_instance);
    if (status == A_STATUS_OK) link->comm = &link->comm_instance;
#endif
#if APP_MODBUS_CLIENT_ENABLE
    link->previous_status = A_STATUS_NOT_READY;
#endif
    return status;
}

static void modbusTask(void *argument)
{
    protocolLink_t *link = argument;
    for (;;) {
        aCommProcessResult_t result;
        const uint32_t started = aOSGetUptimeMs();
        aStatus_t status = aCommProcess(link->comm, &result);
        const uint32_t elapsed = aOSGetUptimeMs() - started;
        /* 单写者更新峰值；Shell 只读取，不加锁、不分配内存。 */
        if (elapsed > aAtomicLoad(&link->max_process_ms))
            aAtomicStore(&link->max_process_ms, elapsed);
        const aStatus_t execution = status == A_STATUS_OK &&
            result.type != ACOMM_WORK_NONE ? result.execution.status : status;
        if (status == A_STATUS_OK && result.type != ACOMM_WORK_NONE)
            aAtomicFetchAdd(&link->work_count, 1U);
        if (execution == A_STATUS_TIMEOUT)
            aAtomicFetchAdd(&link->timeout_count, 1U);
        else if (execution != A_STATUS_OK)
            aAtomicFetchAdd(&link->error_count, 1U);
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

static aStatus_t modbusPrepare(protocolLink_t *link,
    const aCommConfig_t *config)
{
    rs485Port_t port;
    aCommChannelConfig_t framing;
    aStatus_t status;

    status = link->config->prepare(&port);
    if (status != A_STATUS_OK) return status;
    if (port.baud_rate != APP_RS485_BAUD_RATE ||
        port.character_bits != APP_RS485_CHARACTER_BITS) {
        return A_STATUS_INVALID_PARAM;
    }
    aCommChannelConfigStructInit(&framing);
    framing.mode = ACOMM_CHANNEL_GAP;
    framing.stream = port.output;
    framing.timing = link->config->timing;
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
    link->channel_instance.storage = &link->channel_storage;
    status = aCommChannelInitStatic(&framing, &link->channel_instance);
    if (status == A_STATUS_OK) link->channel = &link->channel_instance;
#endif
    if (status != A_STATUS_OK) return status;
    link->protocol.channel = link->channel;
#if !AMODBUS_DYNAMIC_ENABLE
    link->protocol.protocol_storage = &link->modbus_instance;
    link->protocol.mapping_storage = &link->mapping_instance;
    link->mapping_instance.links = link->map_links;
    link->mapping_instance.map_capacity = PROTOCOL_MAP_CAPACITY;
#if ABUS_EVENT_ENABLE
    for (size_t i = 0U; i < sizeof(link->subscriptions) /
         sizeof(link->subscriptions[0]); i++) {
        link->subscriptions[i].native = &link->nodes[i];
    }
#if APP_MODBUS_CLIENT_ENABLE
    link->mapping_instance.comm.requests = link->binding_requests;
    link->mapping_instance.comm.request_capacity = 4U;
#endif
    link->mapping_instance.comm.subscriptions = link->subscriptions;
    link->mapping_instance.comm.subscription_capacity =
        sizeof(link->subscriptions) / sizeof(link->subscriptions[0]);
#endif
#endif
    status = commInit(link, config);
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
    if (link->comm != NULL || link->channel != NULL) return A_STATUS_BUSY;
    link->bus = bus;
    if (bus == NULL) return A_STATUS_INVALID_PARAM;
    if (link->config->task.name == NULL ||
        link->config->task.priority < AOS_TASK_PRIO_LOWEST ||
        link->config->task.priority > AOS_TASK_PRIO_REALTIME) {
        return A_STATUS_INVALID_PARAM;
    }
    status = aBusEndpointInit(&link->data_endpoint, bus);
    if (status != A_STATUS_OK) return status;
    link->protocol.binding.data = &link->data_endpoint.base;
#if APP_MODBUS_CLIENT_ENABLE && ABUS_EVENT_ENABLE
    link->protocol.binding.request_capacity = 4U;
    link->protocol.submit = requestedSubmit;
    link->protocol.submit_context = link;
#endif
    aCommConfig_t config = *link->config->product;
    config.protocol.context = &link->protocol;
    /* 板级链路是 RTU；通用注册本身也支持其他端口及协议。 */
    const aModbusCommConfig_t *product = config.protocol.config;
    if (product == NULL) return A_STATUS_INVALID_PARAM;
    if (product->transport_type != AMODBUS_TRANSPORT_RTU) {
        return A_STATUS_UNSUPPORTED;
    }
    link->filter = (aModbusRtuFilter_t){
        .role = product->role, .unit_id = product->unit_id
    };
    status = modbusPrepare(link, &config);
    if (status == A_STATUS_OK) {
        task = link->config->task;
        task.function = modbusTask;
        task.argument = link;
        status = aOSCreateTask(&task, NULL);
    }
    if (status != A_STATUS_OK) {
        if (link->channel == NULL) return status;
        cleanup_status = modbusDeInit(link);
        return cleanup_status == A_STATUS_OK ? status : cleanup_status;
    }
#if ASHELL_ENABLE
    ASHELL_PRINT("Modbus RTU %s: unit %u\r\n",
        product->role == AMODBUS_ROLE_CLIENT ? "master" : "slave",
        (unsigned)product->unit_id);
#endif
    return A_STATUS_OK;
}

static aStatus_t modbusDeInit(protocolLink_t *link)
{
    aStatus_t status;

    if (link->comm == NULL && link->channel == NULL) {
        return A_STATUS_NOT_READY;
    }
    if (link->comm != NULL) {
#if ACOMM_DYNAMIC_ENABLE
        status = aCommDestroy(link->comm);
#else
        status = aCommDeInitStatic(link->comm);
#endif
        if (status != A_STATUS_OK) return status;
        link->comm = NULL;
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
    return aCommTrigger(rs485_link.comm);
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
        status = aModbusClientWrite(rs485_link.protocol.protocol, &access);
    } else {
        aModbusClientReadRequest_t access = request->access.read;
        access.result = &response;
        status = aModbusClientRead(rs485_link.protocol.protocol, &access);
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

aStatus_t protocolGetStats(protocolStats_t *stats)
{
    if (stats == NULL) return A_STATUS_INVALID_PARAM;
#if APP_MODBUS_ENABLE
    if (rs485_link.comm == NULL) return A_STATUS_NOT_READY;
    aCommChannelStats_t channel;
    aStatus_t status = aCommChannelGetStats(rs485_link.channel, &channel);
    if (status != A_STATUS_OK) return status;
    *stats = (protocolStats_t){
        .work_count = aAtomicLoad(&rs485_link.work_count),
        .error_count = aAtomicLoad(&rs485_link.error_count),
        .timeout_count = aAtomicLoad(&rs485_link.timeout_count),
        .max_process_ms = aAtomicLoad(&rs485_link.max_process_ms),
        .dropped_frames = channel.dropped_frames,
        .queued_frames = channel.queued_frames
    };
    return A_STATUS_OK;
#else
    return A_STATUS_UNSUPPORTED;
#endif
}

#if ABUS_EVENT_ENABLE && APP_MODBUS_ENABLE
aStatus_t protocolRequest(const aBusRequestConfig_t *config,
    aBusRequest_t *request, aBusDispatchResult_t *result)
{
    if (result != NULL) *result = (aBusDispatchResult_t){0};
    if (rs485_link.bus == NULL) return A_STATUS_NOT_READY;
    return aBusRequest(rs485_link.bus, config, request, result);
}
#endif
