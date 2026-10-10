#include "protocol.h"
#include "app_config.h"
#if ABUS_ENABLE
#include "IDU_sig_table.h"
#include "FAN_sig_table.h"
#include "aBus.h"
#include "data_bus_service.h"
#include "sig_task.h"
#include <stddef.h>
#if !ABUS_DYNAMIC_ENABLE
#include "aBus_instance.h"
#endif

/* 先生成命名字段组，再展开 SIG；两者与头文件中的索引来自同一清单。
 * 默认值、规则和字段数组均有静态存储期，生命周期覆盖实例。
 */
#define ABUS_SIG(name, key_value, ...)
#define ABUS_PARAM(name, ...) [name] = {__VA_ARGS__},
#define ABUS_PARAMS(group, ...) \
    static const aBusParam_t group##_params[] = {__VA_ARGS__};
#include "sig/IDU_sig.inc"
#include "sig/FAN_sig.inc"
#undef ABUS_PARAMS
#undef ABUS_PARAM
#undef ABUS_SIG

#define ABUS_PARAMS(group, ...)
#define ABUS_PARAM_REF(group) \
    .params = group##_params, \
    .param_count = sizeof(group##_params) / sizeof(group##_params[0])
#define ABUS_SIG(name, key_value, ...) \
    {.sigKey = (key_value), __VA_ARGS__},

static const aBusSig_t IDU_sigs[] = {
#include "sig/IDU_sig.inc"
};
static const aBusSig_t FAN_sigs[] = {
#include "sig/FAN_sig.inc"
};
#undef ABUS_SIG
#undef ABUS_PARAM_REF
#undef ABUS_PARAMS

/* 直接借用 Flash 中的连续表描述，不在初始化时复制到 RAM。 */
static const aBusTable_t tables[] = {
    {
        .sigs = IDU_sigs,
        .sig_count = IDU_SIG_COUNT,
        .deviceID = IDU_SIG_DEVICE_ID
    },
    {
        .sigs = FAN_sigs,
        .sig_count = FAN_SIG_COUNT,
        .deviceID = FAN_SIG_DEVICE_ID
    }
};

/* FAN 本地副本由协议模块持有；Counter 继续绑定任务自己的变量。 */
static FANMotor_t motor_storage;
ABUS_RAM_BIND_EXPORT(motor_binding,
                     PROTOCOL_BUS_INSTANCE_ID,
                     FAN_SIG_DEVICE_ID,
                     FAN_SIG_MOTOR,
                     motor_storage);
#if !ABUS_DYNAMIC_ENABLE
static aBusHandle_t bus_instance;
static aBusSigState_t bus_states[IDU_SIG_COUNT + FAN_SIG_COUNT];
#endif

static aStatus_t tablesInit(void)
{
    dataBusConfig_t config;

    dataBusConfigStructInit(&config);
    config.instanceID = PROTOCOL_BUS_INSTANCE_ID;
    config.tables = tables;
    config.table_count = sizeof(tables) / sizeof(tables[0]);
#if !ABUS_DYNAMIC_ENABLE
    aBusInstanceStructInit(&bus_instance, bus_states,
                          IDU_SIG_COUNT + FAN_SIG_COUNT);
    config.instance = &bus_instance;
#endif
    return dataBusInit(&config);
}
#endif

#if APP_MODBUS_ENABLE
#include "aModbus.h"
#include "rs485_device.h"
#include "data_bus_modbus.h"
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
extern const aModbusServiceConfig_t FAN_modbus_master_config;

static const aModbusServiceConfig_t *const settings =
    &FAN_modbus_master_config;
#else
extern const aModbusServiceConfig_t IDU_modbus_slave_config;

static const aModbusServiceConfig_t *const settings =
    &IDU_modbus_slave_config;
#endif

/* 协议与 RTU 由 protocol 私有持有；devices 仅持有 USART。 */
static aModbusHandle_t *modbus;
static aModbusRtuHandle_t *rtu;
#if APP_MODBUS_MASTER_ENABLE
static size_t poll_index;
#endif
#if !AMODBUS_DYNAMIC_ENABLE
static aModbusHandle_t modbus_instance;
static aModbusRtuHandle_t rtu_instance;
#endif

static aStatus_t modbusDeInit(void);

/* aStream 不带 context，由本应用端口的适配函数选择 RTU 实例。 */
static aSSize_t readFrame(void *data, size_t size, aTimeout_t timeout)
{
    return aModbusRtuRead(rtu, data, size, timeout);
}

static aSSize_t writeFrame(const void *data, size_t size, aTimeout_t timeout)
{
    return aModbusRtuWrite(rtu, data, size, timeout);
}

static aStatus_t modbusProcess(void)
{
#if APP_MODBUS_MASTER_ENABLE
    const aModbusClientSigRequest_t *request;
    aStatus_t status;

    if (modbus == NULL) return A_STATUS_NOT_READY;
    request = &settings->polls[poll_index];
    status = aModbusClientReadSig(modbus, request);
    if (status == A_STATUS_BUSY) return status;
    /* 失败也推进下标，避免一个离线测点阻塞后续采集项。 */
    poll_index++;
    if (poll_index == settings->poll_count) poll_index = 0U;
    return status;
#else
    if (modbus == NULL) return A_STATUS_NOT_READY;
    return aModbusServerProcess(modbus, &settings->server);
#endif
}

static void modbusTask(void *argument)
{
#if ASHELL_ENABLE && APP_MODBUS_MASTER_ENABLE
    aStatus_t previous = A_STATUS_NOT_READY;
#endif

    (void)argument;
    for (;;) {
        aStatus_t status = modbusProcess();
        uint32_t delay = status == A_STATUS_OK ? settings->interval_ms :
                                                settings->error_delay_ms;

#if ASHELL_ENABLE && APP_MODBUS_MASTER_ENABLE
        if (status != previous) {
            ASHELL_PRINT("Modbus master: %s (status %d)\r\n",
                         status == A_STATUS_OK ? "online" : "read failed",
                         (int)status);
            previous = status;
        }
#endif
        if (delay != 0U) aOSDelayMs(delay);
    }
}

static aStatus_t modbusPrepare(void)
{
    rs485Port_t port;
    aModbusRtuConfig_t framing;
    aModbusConfig_t config;
    aStatus_t status;

    status = rs485PortPrepare(&port);
    if (status != A_STATUS_OK) return status;
    aModbusRtuConfigStructInit(&framing);
    framing.role = settings->modbus.role;
    framing.unit_id = settings->modbus.unit_id;
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
    status = aModbusRtuCreate(&framing, &rtu);
#else
    status = aModbusRtuInitStatic(&framing, &rtu_instance);
    if (status == A_STATUS_OK) rtu = &rtu_instance;
#endif
    if (status != A_STATUS_OK) return status;

    /* 复制完整协议配置，仅装配当前端口和系统私有总线。 */
    config = settings->modbus;
    aModbusTransportStructInit(&config.transport);
    config.transport.stream.read = readFrame;
    config.transport.stream.write = writeFrame;
    config.transport.stream.flush = port.output.flush;
    status = aModbusRtuBindTransport(rtu, &config.transport);
    if (status != A_STATUS_OK) return status;
#if AMODBUS_DYNAMIC_ENABLE
    status = dataBusModbusCreate(&config, &modbus);
#else
    status = dataBusModbusInitStatic(&config, &modbus_instance);
    if (status == A_STATUS_OK) modbus = &modbus_instance;
#endif
    if (status != A_STATUS_OK) return status;
    /* 两个协议实例都已就绪，才允许 USART ISR 交付字节。 */
    return rs485PortOpen(aModbusRtuReceive, rtu);
}

static aStatus_t modbusInit(void)
{
    aOSTaskConfig_t task;
    aStatus_t status;
    aStatus_t cleanup_status;

    if (modbus != NULL || rtu != NULL) return A_STATUS_BUSY;
    if (settings->modbus.transport_type != AMODBUS_TRANSPORT_RTU) {
        return A_STATUS_UNSUPPORTED;
    }
    if (!aTimeoutIsValid(settings->modbus.byte_timeout) ||
        settings->task.name == NULL ||
        settings->task.priority < AOS_TASK_PRIO_LOWEST ||
        settings->task.priority > AOS_TASK_PRIO_REALTIME ||
        settings->error_delay_ms == 0U) {
        return A_STATUS_INVALID_PARAM;
    }
#if APP_MODBUS_MASTER_ENABLE
    if (settings->modbus.role != AMODBUS_ROLE_CLIENT ||
        settings->polls == NULL ||
        settings->poll_count == 0U) return A_STATUS_INVALID_PARAM;
    poll_index = 0U;
#else
    if (settings->modbus.role != AMODBUS_ROLE_SERVER ||
        !aTimeoutIsValid(settings->server.timeout) ||
        (settings->server.timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
         settings->server.timeout.milliseconds == 0U &&
         settings->interval_ms == 0U)) return A_STATUS_INVALID_PARAM;
#endif
    status = modbusPrepare();
    if (status == A_STATUS_OK) {
        task = settings->task;
        task.function = modbusTask;
        task.argument = NULL;
        status = aOSCreateTask(&task, NULL);
    }
    if (status != A_STATUS_OK) {
        /* 没有协议资源时不触碰可能被其他使用方占用的物理端口。 */
        if (modbus == NULL && rtu == NULL) return status;
        cleanup_status = modbusDeInit();
        return cleanup_status == A_STATUS_OK ? status : cleanup_status;
    }
#if ASHELL_ENABLE
    ASHELL_PRINT("Modbus RTU %s: unit %u\r\n",
                 settings->modbus.role == AMODBUS_ROLE_CLIENT ?
                     "master" : "slave",
                 (unsigned)settings->modbus.unit_id);
#endif
    return A_STATUS_OK;
}

static aStatus_t modbusDeInit(void)
{
    aStatus_t status;

    if (modbus == NULL && rtu == NULL) return A_STATUS_NOT_READY;
    if (modbus != NULL) {
#if AMODBUS_DYNAMIC_ENABLE
        status = aModbusDestroy(modbus);
#else
        status = aModbusDeInitStatic(modbus);
#endif
        if (status != A_STATUS_OK) return status;
        modbus = NULL;
    }
    status = rs485PortClose();
    if (status != A_STATUS_OK && status != A_STATUS_NOT_READY) return status;
    /* Close 成功后 ISR 不再使用 RTU，才能释放接收状态。 */
    if (rtu != NULL) {
#if AMODBUS_DYNAMIC_ENABLE
        status = aModbusRtuDestroy(rtu);
#else
        status = aModbusRtuDeInitStatic(rtu);
#endif
        if (status != A_STATUS_OK) return status;
        rtu = NULL;
    }
    return A_STATUS_OK;
}
#endif

/* 仅启动阶段使用；失败后保留已启动的任务，禁止重复创建。 */
aStatus_t protocolInit(void)
{
    static aBool_t initialization_started;
#if ABUS_ENABLE || APP_MODBUS_ENABLE
    aStatus_t status;
#endif

    if (initialization_started) return A_STATUS_BUSY;
    initialization_started = A_TRUE;
#if ABUS_ENABLE
    status = tablesInit();
    if (status != A_STATUS_OK) return status;
    status = appSigTaskInit();
    if (status != A_STATUS_OK) return status;
#endif
#if APP_MODBUS_ENABLE
    status = modbusInit();
    if (status != A_STATUS_OK) return status;
#endif
    return A_STATUS_OK;
}
