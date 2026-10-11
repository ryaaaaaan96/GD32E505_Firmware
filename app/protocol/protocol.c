#include "protocol.h"
#include "app_config.h"

#if APP_MODBUS_ENABLE
#include "aModbus.h"
#include "rs485_device.h"
#include "aModbus_rtu.h"
#if !AMODBUS_DYNAMIC_ENABLE
#include "aModbus_instance.h"
#include "aModbus_rtu_instance.h"
#endif
#if ASHELL_ENABLE
#include "aShell.h"
#endif

/* 只读配置在设备 .c 中定义，按当前角色声明并装配。 */
#if APP_MODBUS_MASTER_ENABLE
#if !AMODBUS_CLIENT_ENABLE
#error "Modbus master demo requires AMODBUS_CLIENT_ENABLE"
#endif
extern const aModbusServiceConfig_t FAN_modbus_master_config;
#else
#if !AMODBUS_SERVER_ENABLE
#error "Modbus slave demo requires AMODBUS_SERVER_ENABLE"
#endif
extern const aModbusServiceConfig_t IDU_modbus_slave_config;
#endif

/* 每条通信链路独立持有状态；多个链路可借用同一个 aBus。
 * 当前硬件只注册 USART2，后续新端口增加自己的收发适配函数。
 */
typedef struct {
    const aModbusServiceConfig_t *service;
    aOSTaskConfig_t task; /* 本链路的任务资源，入口和参数在创建时填充。 */
    aStatus_t (*prepare)(rs485Port_t *port);
    aStatus_t (*open)(rs485ReceiveFn_t receive, void *context);
    aStatus_t (*close)(void);
    aStream_t framing_stream;
} protocolLinkConfig_t;

typedef struct {
    const protocolLinkConfig_t *config;
    const aModbusServiceConfig_t *settings;
    aModbusHandle_t *modbus;
    aModbusRtuHandle_t *rtu;
    size_t poll_index;
    uint32_t cycle_started;
    aBool_t cycle_complete;
#if !AMODBUS_DYNAMIC_ENABLE
    aModbusHandle_t modbus_instance;
    aModbusRtuHandle_t rtu_instance;
#endif
} protocolLink_t;

static const protocolLinkConfig_t rs485_config;
static protocolLink_t rs485_link = {.config = &rs485_config};
static aStatus_t modbusDeInit(protocolLink_t *link);

/* aStream 不带 context，由本应用端口的适配函数选择 RTU 实例。 */
static aSSize_t readFrame(void *data, size_t size, aTimeout_t timeout)
{
    return aModbusRtuRead(rs485_link.rtu, data, size, timeout);
}

static aSSize_t writeFrame(const void *data, size_t size, aTimeout_t timeout)
{
    return aModbusRtuWrite(rs485_link.rtu, data, size, timeout);
}

/* 每条硬件链路注册自己的配置；总线无需知道协议或端口。 */
static const protocolLinkConfig_t rs485_config = {
#if APP_MODBUS_MASTER_ENABLE
    .service = &FAN_modbus_master_config,
#else
    .service = &IDU_modbus_slave_config,
#endif
    .task = {
        .name = "modbus",
        .stack_bytes = 4096U,
        .priority = AOS_TASK_PRIO_NORMAL,
    },
    .prepare = rs485PortPrepare,
    .open = rs485PortOpen,
    .close = rs485PortClose,
    .framing_stream = {.read = readFrame, .write = writeFrame}
};

static aStatus_t modbusProcess(protocolLink_t *link)
{
    link->cycle_complete = A_FALSE;
    if (link->modbus == NULL) return A_STATUS_NOT_READY;
#if AMODBUS_CLIENT_ENABLE
    if (link->settings->modbus.role == AMODBUS_ROLE_CLIENT) {
        size_t completed;
        const aModbusClientBatchRequest_t request = {
            .items = &link->settings->polls[link->poll_index],
            .count = link->settings->poll_count - link->poll_index,
            .completed = &completed,
            .unit_id = link->settings->modbus.unit_id,
            .timeout = link->settings->request_timeout
        };
        aStatus_t status = aModbusClientReadBatch(link->modbus, &request);
        if (completed == 0U) return status;
        /* 合并的连续地址一次提交；失败也推进，避免离线项阻塞整轮。 */
        link->poll_index += completed;
        if (link->poll_index == link->settings->poll_count) {
            link->poll_index = 0U;
            link->cycle_complete = A_TRUE;
        }
        return status;
    }
#endif
#if AMODBUS_SERVER_ENABLE
    return aModbusServerProcess(link->modbus, &link->settings->server);
#else
    return A_STATUS_UNSUPPORTED;
#endif
}

static void modbusTask(void *argument)
{
#if ASHELL_ENABLE && AMODBUS_CLIENT_ENABLE
    aStatus_t previous = A_STATUS_NOT_READY;
#endif

    protocolLink_t *link = argument;
    link->cycle_started = aOSGetUptimeMs();
    for (;;) {
        aStatus_t status = modbusProcess(link);
        uint32_t delay = status == A_STATUS_OK ? link->settings->interval_ms :
                                                link->settings->error_delay_ms;

#if ASHELL_ENABLE && AMODBUS_CLIENT_ENABLE
        if (link->settings->modbus.role == AMODBUS_ROLE_CLIENT &&
            status != previous) {
            ASHELL_PRINT("Modbus master: %s (status %d)\r\n",
                         status == A_STATUS_OK ? "online" : "read failed",
                         (int)status);
            previous = status;
        }
#endif
#if AMODBUS_CLIENT_ENABLE
        if (link->settings->modbus.role == AMODBUS_ROLE_CLIENT &&
            link->cycle_complete) {
            uint32_t elapsed = aOSGetUptimeMs() - link->cycle_started;
            uint32_t remaining = elapsed < link->settings->poll_time_ms ?
                link->settings->poll_time_ms - elapsed : 0U;
            if (remaining > delay) delay = remaining;
            if (delay != 0U) aOSDelayMs(delay);
            link->cycle_started = aOSGetUptimeMs();
        } else
#endif
        if (delay != 0U) aOSDelayMs(delay);
    }
}

static aStatus_t modbusPrepare(protocolLink_t *link, aBusHandle_t *bus)
{
    rs485Port_t port;
    aModbusRtuConfig_t framing;
    aModbusConfig_t config;
    aStatus_t status;

    status = link->config->prepare(&port);
    if (status != A_STATUS_OK) return status;
    aModbusRtuConfigStructInit(&framing);
    framing.role = link->settings->modbus.role;
    framing.unit_id = link->settings->modbus.unit_id;
    framing.baud_rate = port.baud_rate;
    framing.character_bits = port.character_bits;
    framing.output = port.output;
    framing.io.ticks_per_second = port.ticks_per_second;
    framing.io.ticks = port.ticks;
    framing.io.enter = port.enter;
    framing.io.exit = port.exit;
    framing.io.wait_transmit_complete = port.wait_transmit_complete;
    framing.io.clear_error = port.clear_error;
#if AMODBUS_DYNAMIC_ENABLE
    status = aModbusRtuCreate(&framing, &link->rtu);
#else
    status = aModbusRtuInitStatic(&framing, &link->rtu_instance);
    if (status == A_STATUS_OK) link->rtu = &link->rtu_instance;
#endif
    if (status != A_STATUS_OK) return status;

    /* 复制完整协议配置，仅装配当前端口和系统私有总线。 */
    config = link->settings->modbus;
    config.bus = bus;
    aModbusTransportStructInit(&config.transport);
    config.transport.stream = link->config->framing_stream;
    config.transport.stream.flush = port.output.flush;
    status = aModbusRtuBindTransport(link->rtu, &config.transport);
    if (status != A_STATUS_OK) return status;
#if AMODBUS_DYNAMIC_ENABLE
    status = aModbusCreate(&config, &link->modbus);
#else
    status = aModbusInitStatic(&config, &link->modbus_instance);
    if (status == A_STATUS_OK) link->modbus = &link->modbus_instance;
#endif
    if (status != A_STATUS_OK) return status;
    /* 两个协议实例都已就绪，才允许 USART ISR 交付字节。 */
    return link->config->open(aModbusRtuReceive, link->rtu);
}

static aStatus_t modbusInit(protocolLink_t *link, aBusHandle_t *bus)
{
    aOSTaskConfig_t task;
    aStatus_t status;
    aStatus_t cleanup_status;

    if (link->config == NULL || link->config->service == NULL ||
        link->config->prepare == NULL || link->config->open == NULL ||
        link->config->close == NULL) return A_STATUS_INVALID_PARAM;
    if (link->modbus != NULL || link->rtu != NULL) return A_STATUS_BUSY;
    link->settings = link->config->service;
    if (bus == NULL) return A_STATUS_INVALID_PARAM;
    if (link->settings->modbus.transport_type != AMODBUS_TRANSPORT_RTU) {
        return A_STATUS_UNSUPPORTED;
    }
    if (!aTimeoutIsValid(link->settings->modbus.byte_timeout) ||
        link->config->task.name == NULL ||
        link->config->task.priority < AOS_TASK_PRIO_LOWEST ||
        link->config->task.priority > AOS_TASK_PRIO_REALTIME ||
        link->settings->error_delay_ms == 0U) {
        return A_STATUS_INVALID_PARAM;
    }
    if (link->settings->modbus.role == AMODBUS_ROLE_CLIENT) {
#if AMODBUS_CLIENT_ENABLE
        aModbusConfig_t config = link->settings->modbus;
        if (link->settings->polls == NULL ||
            link->settings->poll_count == 0U ||
            link->settings->poll_time_ms > INT32_MAX ||
            !aTimeoutIsValid(link->settings->request_timeout)) {
            return A_STATUS_INVALID_PARAM;
        }
        config.bus = bus;
        for (size_t i = 0U; i < link->settings->poll_count; i++) {
            status = aModbusClientPollCheck(&config,
                &link->settings->polls[i], config.unit_id);
            if (status != A_STATUS_OK) return status;
        }
        link->poll_index = 0U;
        link->cycle_started = aOSGetUptimeMs();
#else
        return A_STATUS_UNSUPPORTED;
#endif
    } else if (link->settings->modbus.role == AMODBUS_ROLE_SERVER) {
#if AMODBUS_SERVER_ENABLE
        if (!aTimeoutIsValid(link->settings->server.timeout) ||
            (link->settings->server.timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
             link->settings->server.timeout.milliseconds == 0U &&
             link->settings->interval_ms == 0U)) return A_STATUS_INVALID_PARAM;
#else
        return A_STATUS_UNSUPPORTED;
#endif
    } else {
        return A_STATUS_INVALID_PARAM;
    }
    status = modbusPrepare(link, bus);
    if (status == A_STATUS_OK) {
        task = link->config->task;
        task.function = modbusTask;
        task.argument = link;
        status = aOSCreateTask(&task, NULL);
    }
    if (status != A_STATUS_OK) {
        /* 没有协议资源时不触碰可能被其他使用方占用的物理端口。 */
        if (link->modbus == NULL && link->rtu == NULL) return status;
        cleanup_status = modbusDeInit(link);
        return cleanup_status == A_STATUS_OK ? status : cleanup_status;
    }
#if ASHELL_ENABLE
    ASHELL_PRINT("Modbus RTU %s: unit %u\r\n",
                 link->settings->modbus.role == AMODBUS_ROLE_CLIENT ?
                     "master" : "slave",
                 (unsigned)link->settings->modbus.unit_id);
#endif
    return A_STATUS_OK;
}

static aStatus_t modbusDeInit(protocolLink_t *link)
{
    aStatus_t status;

    if (link->modbus == NULL && link->rtu == NULL) return A_STATUS_NOT_READY;
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
    /* Close 成功后 ISR 不再使用 RTU，才能释放接收状态。 */
    if (link->rtu != NULL) {
#if AMODBUS_DYNAMIC_ENABLE
        status = aModbusRtuDestroy(link->rtu);
#else
        status = aModbusRtuDeInitStatic(link->rtu);
#endif
        if (status != A_STATUS_OK) return status;
        link->rtu = NULL;
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
