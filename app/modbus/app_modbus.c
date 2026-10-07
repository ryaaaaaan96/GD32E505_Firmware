#include "app_modbus.h"
#include "app_modbus_port.h"
#include "app_sig_modbus.h"
#include "app_sig_ids.h"
#if !AMODBUS_DYNAMIC_ENABLE
#include "aModbus_instance.h"
#endif
#if ASHELL_ENABLE
#include "aShell.h"
#endif

static aModbusHandle_t *modbus;
#if !AMODBUS_DYNAMIC_ENABLE
static aModbusHandle_t modbus_instance;
#endif

#if !APP_MODBUS_MASTER_ENABLE
/* 地址是从零开始的保持寄存器偏移；各 32 位值均为高字在前。 */
static const aModbusBusMap_t maps[] = {
    {
        .address = 0U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {APP_SIG_DEVICE_ID, APP_BUS_COUNTER, AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .address = 2U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {APP_SIG_DEVICE_ID, APP_BUS_MOTOR, 0U},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .address = 4U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {APP_SIG_DEVICE_ID, APP_BUS_MOTOR, 1U},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
};
static const aModbusAddressRange_t ranges[] = {
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 0U,
        .quantity = 6U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .maps = maps,
        .map_count = sizeof(maps) / sizeof(maps[0]),
    },
};
#endif

aStatus_t appModbusProcess(void)
{
    aStatus_t status;
#if APP_MODBUS_MASTER_ENABLE
    aModbusClientSigRequest_t request;

    if (modbus == NULL) return A_STATUS_NOT_READY;
    aModbusClientSigRequestStructInit(&request);
    request.unit_id = APP_MODBUS_UNIT_ID;
    request.area = AMODBUS_AREA_HOLDING_REGISTERS;
    request.address = 2U;
    request.target.deviceID = APP_SIG_DEVICE_ID;
    request.target.sigIndex = APP_BUS_MOTOR;
    request.target.paramIndex = 0U;
    request.word_order = AMODBUS_WORD_HIGH_FIRST;
    request.timeout = A_TIMEOUT_MS(500U);
    /* 失败保留本地旧值；只有完整读回并通过范围校验才发布。 */
    status = aModbusClientReadSig(modbus, &request);
#else
    aModbusServerProcessRequest_t request;

    if (modbus == NULL) return A_STATUS_NOT_READY;
    aModbusServerProcessRequestStructInit(&request);
    request.timeout = A_TIMEOUT_MS(500U);
    status = aModbusServerProcess(modbus, &request);
#endif
    appModbusPortFrameReset();
    return status;
}

aStatus_t appModbusInit(void)
{
    aModbusConfig_t config;
    aStatus_t status;

    if (modbus != NULL) return A_STATUS_BUSY;
    aModbusConfigStructInit(&config);
    config.transport_type = AMODBUS_TRANSPORT_RTU;
    config.unit_id = APP_MODBUS_UNIT_ID;
#if APP_MODBUS_MASTER_ENABLE
    config.role = AMODBUS_ROLE_CLIENT;
#else
    config.role = AMODBUS_ROLE_SERVER;
    config.ranges = ranges;
    config.range_count = sizeof(ranges) / sizeof(ranges[0]);
#endif
    status = appModbusPortInit(&config.transport);
    if (status != A_STATUS_OK) return status;
#if AMODBUS_DYNAMIC_ENABLE
    status = appSigModbusCreate(&config, &modbus);
#else
    status = appSigModbusInitStatic(&config, &modbus_instance);
    if (status == A_STATUS_OK) modbus = &modbus_instance;
#endif
    if (status != A_STATUS_OK) {
        (void)appModbusPortDeInit();
        return status;
    }

#if ASHELL_ENABLE
    ASHELL_PRINT("Modbus RTU %s: USART2 PC10/PC11, DE PA15, "
                 "115200 8N1, %s %u\r\n",
                 APP_MODBUS_MASTER_ENABLE ? "master" : "slave",
                 APP_MODBUS_MASTER_ENABLE ? "target" : "unit",
                 (unsigned)APP_MODBUS_UNIT_ID);
#endif
    return A_STATUS_OK;
}

aStatus_t appModbusDeInit(void)
{
    aStatus_t status;
    if (modbus != NULL) {
#if AMODBUS_DYNAMIC_ENABLE
        status = aModbusDestroy(modbus);
#else
        status = aModbusDeInitStatic(modbus);
#endif
        if (status != A_STATUS_OK) return status;
        modbus = NULL;
    }
    /* 端口仍有在途发送时允许稍后再次调用，继续释放剩余资源。 */
    return appModbusPortDeInit();
}
