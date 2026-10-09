#include "aModbus_rtu_usart_instance.h"
#include "aModbus_rtu_internal.h"
#include "aDrv_basic.h"
#include "aOS.h"

void aModbusRtuUsartConfigStructInit(aModbusRtuUsartConfig_t *config)
{
    if (config != NULL) {
        config->role = AMODBUS_SERVER_ENABLE ? AMODBUS_ROLE_SERVER :
                                               AMODBUS_ROLE_CLIENT;
        config->unit_id = 1U;
        aDevUsartConfigStructInit(&config->usart);
        config->usart.mode = ADEV_USART_TX_INTERRUPT_BUFFERED |
                             ADEV_USART_RX_INTERRUPT_CALLBACK;
    }
}

static uint32_t ticks(void *context)
{
    (void)context;
    return aDrvCycleCounterRead();
}

static void enter(void *context)
{
    (void)context;
    aOSCriticalEnter();
}

static void leave(void *context)
{
    (void)context;
    aOSCriticalExit();
}

static aSSize_t write_bytes(void *context, const void *data, size_t size,
                           aTimeout_t timeout)
{
    aModbusRtuUsartHandle_t *handle = context;
    return aDevUsartWrite(handle->usart, data, size, timeout);
}

static aStatus_t wait_transmit(void *context, aTimeout_t timeout)
{
    aModbusRtuUsartHandle_t *handle = context;
    return aDevUsartWaitTransmitComplete(handle->usart, timeout);
}

static void clear_error(void *context)
{
    aModbusRtuUsartHandle_t *handle = context;
    aDevUsartClearRxError(handle->usart);
}

static aStatus_t instance_init(const aModbusRtuUsartConfig_t *config,
                              aModbusRtuUsartHandle_t *handle)
{
    aModbusRtuConfig_t rtu;
    aDevUsartConfig_t serial;
    aStatus_t status;
    if (config == NULL || handle == NULL) return A_STATUS_INVALID_PARAM;
    serial = config->usart;
    if ((serial.mode & ADEV_USART_RX_MASK) !=
            ADEV_USART_RX_INTERRUPT_CALLBACK ||
        serial.rx_byte_callback != NULL || serial.rx_byte_context != NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (serial.drv_config.stop_bits != ADRV_USART_STOP_1 &&
        serial.drv_config.stop_bits != ADRV_USART_STOP_2) {
        return A_STATUS_INVALID_PARAM;
    }
    if (serial.drv_config.parity != ADRV_USART_PARITY_NONE &&
        serial.drv_config.parity != ADRV_USART_PARITY_EVEN &&
        serial.drv_config.parity != ADRV_USART_PARITY_ODD) {
        return A_STATUS_INVALID_PARAM;
    }
    aModbusRtuConfigStructInit(&rtu);
    rtu.role = config->role;
    rtu.unit_id = config->unit_id;
    rtu.baud_rate = serial.drv_config.baud_rate;
    rtu.character_bits = 10U +
        (serial.drv_config.parity != ADRV_USART_PARITY_NONE) +
        (serial.drv_config.stop_bits == ADRV_USART_STOP_2);
    rtu.io.context = handle;
    rtu.io.ticks_per_second = aDrvGetCoreClockHz();
    rtu.io.ticks = ticks;
    rtu.io.enter = enter;
    rtu.io.exit = leave;
    rtu.io.write = write_bytes;
    rtu.io.wait_transmit_complete = wait_transmit;
    rtu.io.clear_error = clear_error;
    status = aDrvCycleCounterEnable();
    if (status != A_STATUS_OK) return status;
    status = aModbusRtuInstanceInit(&rtu, &handle->rtu);
    if (status != A_STATUS_OK) return status;
    handle->usart = NULL;
    handle->dynamic = A_FALSE;
    /* RTU 状态准备好后才允许串口打开 IRQ，回调不会访问半初始化实例。 */
    serial.rx_byte_callback = aModbusRtuReceive;
    serial.rx_byte_context = &handle->rtu;
#if ADEV_USART_DYNAMIC_ENABLE
    status = aDevUsartCreate(&serial, &handle->usart);
#else
    status = aDevUsartInitStatic(&serial, &handle->usart_instance);
    if (status == A_STATUS_OK) handle->usart = &handle->usart_instance;
#endif
    if (status != A_STATUS_OK) handle->rtu.ready = A_FALSE;
    return status;
}

static aStatus_t instance_close(aModbusRtuUsartHandle_t *handle)
{
    aStatus_t status;
    if (!handle->rtu.ready) return A_STATUS_NOT_READY;
    /* 关闭失败时保留 RTU 状态，尚未退出的 ISR 仍可安全使用它。 */
#if ADEV_USART_DYNAMIC_ENABLE
    status = aDevUsartDestroy(handle->usart);
#else
    status = aDevUsartDeInit(handle->usart);
#endif
    if (status == A_STATUS_OK) {
        handle->usart = NULL;
        handle->rtu.ready = A_FALSE;
    }
    return status;
}

#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusRtuUsartInitStatic(const aModbusRtuUsartConfig_t *config,
                                  aModbusRtuUsartHandle_t *handle)
{
    return instance_init(config, handle);
}

aStatus_t aModbusRtuUsartDeInitStatic(aModbusRtuUsartHandle_t *handle)
{
    if (handle == NULL || handle->dynamic) return A_STATUS_INVALID_PARAM;
    return instance_close(handle);
}
#endif

#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusRtuUsartCreate(const aModbusRtuUsartConfig_t *config,
                              aModbusRtuUsartHandle_t **handle_out)
{
    aModbusRtuUsartHandle_t *handle;
    aStatus_t status;
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    handle = aOSAlloc(sizeof(*handle));
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = instance_init(config, handle);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    handle->dynamic = A_TRUE;
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aModbusRtuUsartDestroy(aModbusRtuUsartHandle_t *handle)
{
    aStatus_t status;
    if (handle == NULL) return A_STATUS_OK;
    if (!handle->dynamic) return A_STATUS_INVALID_PARAM;
    status = instance_close(handle);
    if (status == A_STATUS_OK) aOSFree(handle);
    return status;
}
#endif

aStatus_t aModbusRtuUsartGetTransport(aModbusRtuUsartHandle_t *handle,
                                    aModbusTransport_t *transport)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    return aModbusRtuGetTransport(&handle->rtu, transport);
}
