#include "modbus_slave.h"

#if !APP_MODBUS_MASTER_ENABLE
#include "rs485_config.h"
#include "aModbus_rtu_usart.h"
#include "sig_modbus_bind.h"
#include "sig_ids.h"
#if !AMODBUS_DYNAMIC_ENABLE
#include "aModbus_instance.h"
#include "aModbus_rtu_usart_instance.h"
#endif
#if ASHELL_ENABLE
#include "aShell.h"
#endif

#if !AMODBUS_SERVER_ENABLE
#error "Modbus slave demo requires AMODBUS_SERVER_ENABLE"
#endif

static aModbusHandle_t *modbus;
static aModbusRtuUsartHandle_t *rtu;
#if !AMODBUS_DYNAMIC_ENABLE
static aModbusHandle_t modbus_instance;
static aModbusRtuUsartHandle_t rtu_instance;
#endif

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

aStatus_t modbusSlaveProcess(void)
{
    aModbusServerProcessRequest_t request;

    if (modbus == NULL) return A_STATUS_NOT_READY;
    aModbusServerProcessRequestStructInit(&request);
    request.timeout = A_TIMEOUT_MS(500U);
    return aModbusServerProcess(modbus, &request);
}

aStatus_t modbusSlaveInit(void)
{
    aModbusConfig_t config;
    aModbusRtuUsartConfig_t serial;
    aStatus_t status;

    if (modbus != NULL || rtu != NULL) return A_STATUS_BUSY;
    aModbusConfigStructInit(&config);
    config.transport_type = AMODBUS_TRANSPORT_RTU;
    config.unit_id = APP_MODBUS_SLAVE_UNIT_ID;
    config.role = AMODBUS_ROLE_SERVER;
    config.ranges = ranges;
    config.range_count = sizeof(ranges) / sizeof(ranges[0]);
    aModbusRtuUsartConfigStructInit(&serial);
    appRs485ConfigInit(&serial.usart);
    serial.role = config.role;
    serial.unit_id = config.unit_id;
#if AMODBUS_DYNAMIC_ENABLE
    status = aModbusRtuUsartCreate(&serial, &rtu);
#else
    status = aModbusRtuUsartInitStatic(&serial, &rtu_instance);
    if (status == A_STATUS_OK) rtu = &rtu_instance;
#endif
    if (status != A_STATUS_OK) return status;
    status = aModbusRtuUsartGetTransport(rtu, &config.transport);
    if (status != A_STATUS_OK) {
        (void)modbusSlaveDeInit();
        return status;
    }
#if AMODBUS_DYNAMIC_ENABLE
    status = sigModbusCreate(&config, &modbus);
#else
    status = sigModbusInitStatic(&config, &modbus_instance);
    if (status == A_STATUS_OK) modbus = &modbus_instance;
#endif
    if (status != A_STATUS_OK) {
        (void)modbusSlaveDeInit();
        return status;
    }

#if ASHELL_ENABLE
    ASHELL_PRINT("Modbus RTU slave: USART%u, %lu 8%c%u, "
                 "unit %u\r\n",
                 (unsigned)serial.usart.drv_config.id,
                 (unsigned long)serial.usart.drv_config.baud_rate,
                 serial.usart.drv_config.parity == ADRV_USART_PARITY_NONE ?
                     'N' : (serial.usart.drv_config.parity ==
                             ADRV_USART_PARITY_EVEN ? 'E' : 'O'),
                 serial.usart.drv_config.stop_bits == ADRV_USART_STOP_2 ?
                     2U : 1U,
                 (unsigned)APP_MODBUS_SLAVE_UNIT_ID);
#endif
    return A_STATUS_OK;
}

aStatus_t modbusSlaveDeInit(void)
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
    if (rtu == NULL) return A_STATUS_NOT_READY;
#if AMODBUS_DYNAMIC_ENABLE
    status = aModbusRtuUsartDestroy(rtu);
#else
    status = aModbusRtuUsartDeInitStatic(rtu);
#endif
    if (status == A_STATUS_OK) rtu = NULL;
    return status;
}

#endif
