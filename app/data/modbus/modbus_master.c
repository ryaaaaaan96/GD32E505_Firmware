#include "modbus_master.h"

#if APP_MODBUS_MASTER_ENABLE
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

#if !AMODBUS_CLIENT_ENABLE
#error "Modbus master demo requires AMODBUS_CLIENT_ENABLE"
#endif

static aModbusHandle_t *modbus;
static aModbusRtuUsartHandle_t *rtu;
#if !AMODBUS_DYNAMIC_ENABLE
static aModbusHandle_t modbus_instance;
static aModbusRtuUsartHandle_t rtu_instance;
#endif

aStatus_t modbusMasterProcess(void)
{
    aModbusClientSigRequest_t request;

    if (modbus == NULL) return A_STATUS_NOT_READY;
    aModbusClientSigRequestStructInit(&request);
    request.unit_id = APP_MODBUS_MASTER_TARGET_ID;
    request.area = AMODBUS_AREA_HOLDING_REGISTERS;
    request.address = 2U;
    request.target.deviceID = APP_SIG_DEVICE_ID;
    request.target.sigIndex = APP_BUS_MOTOR;
    request.target.paramIndex = 0U;
    request.word_order = AMODBUS_WORD_HIGH_FIRST;
    request.timeout = A_TIMEOUT_MS(500U);
    /* 失败保留本地旧值；只有完整读回并通过范围校验才发布。 */
    return aModbusClientReadSig(modbus, &request);
}

aStatus_t modbusMasterInit(void)
{
    aModbusConfig_t config;
    aModbusRtuUsartConfig_t serial;
    aStatus_t status;

    if (modbus != NULL || rtu != NULL) return A_STATUS_BUSY;
    aModbusConfigStructInit(&config);
    config.transport_type = AMODBUS_TRANSPORT_RTU;
    config.unit_id = APP_MODBUS_MASTER_TARGET_ID;
    config.role = AMODBUS_ROLE_CLIENT;
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
        (void)modbusMasterDeInit();
        return status;
    }
#if AMODBUS_DYNAMIC_ENABLE
    status = sigModbusCreate(&config, &modbus);
#else
    status = sigModbusInitStatic(&config, &modbus_instance);
    if (status == A_STATUS_OK) modbus = &modbus_instance;
#endif
    if (status != A_STATUS_OK) {
        (void)modbusMasterDeInit();
        return status;
    }

#if ASHELL_ENABLE
    ASHELL_PRINT("Modbus RTU master: USART%u, %lu 8%c%u, "
                 "target %u\r\n",
                 (unsigned)serial.usart.drv_config.id,
                 (unsigned long)serial.usart.drv_config.baud_rate,
                 serial.usart.drv_config.parity == ADRV_USART_PARITY_NONE ?
                     'N' : (serial.usart.drv_config.parity ==
                             ADRV_USART_PARITY_EVEN ? 'E' : 'O'),
                 serial.usart.drv_config.stop_bits == ADRV_USART_STOP_2 ?
                     2U : 1U,
                 (unsigned)APP_MODBUS_MASTER_TARGET_ID);
#endif
    return A_STATUS_OK;
}

aStatus_t modbusMasterDeInit(void)
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
