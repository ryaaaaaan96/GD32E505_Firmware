#include "app_modbus_port.h"
#include "app_modbus.h"
#include "aDev_usart.h"
#include "aOS.h"
#if !ADEV_USART_DYNAMIC_ENABLE
#include "aDev_usart_instance.h"
#endif
#include <string.h>

static aDevUsartHandle_t *usart;
#if !ADEV_USART_DYNAMIC_ENABLE
static aDevUsartHandle_t usart_instance;
#endif
static uint8_t rx_buffer[512U];
static uint8_t tx_buffer[256U];
static uint8_t frame[256U];
static size_t frame_size;
static size_t frame_position;
static aBool_t frame_ready;
static uint32_t last_activity;

/* 115200 时采用 4 ms 的任务级空闲窗口，包含 1 ms 时基量化余量。
 * 这是保守的 Demo 收帧方式，不提供严格的 t1.5 微秒级判定。 */
static const uint32_t quiet_ms = 4U;
static const aDevUsartConfig_t usart_config = {
    .drv_config = {
        .id = ADRV_USART_2,
        .baud_rate = 115200U,
        .parity = ADRV_USART_PARITY_NONE,
        .stop_bits = ADRV_USART_STOP_1,
        .tx_pin = ADRV_PIN(ADRV_GPIO_PORT_C, 10),
        .rx_pin = ADRV_PIN(ADRV_GPIO_PORT_C, 11),
    },
    .mode = ADEV_USART_TX_INTERRUPT_BUFFERED |
            ADEV_USART_RX_INTERRUPT_BUFFERED,
    .interrupt_priority = 6U,
    .rx_buffer = rx_buffer,
    .rx_buffer_size = sizeof(rx_buffer),
    .tx_buffer = tx_buffer,
    .tx_buffer_size = sizeof(tx_buffer),
    .rs485 = {
        .mode = ADEV_USART_RS485_GPIO_DE,
        .de_pin = ADRV_PIN(ADRV_GPIO_PORT_A, 15),
        .de_active_level = ADRV_GPIO_HIGH,
    },
};

void appModbusPortFrameReset(void)
{
    frame_size = 0U;
    frame_position = 0U;
    frame_ready = A_FALSE;
}

static aBool_t no_data(void)
{
    aErrno_t error = aOSGetErrno();
    return error == A_EAGAIN || error == A_ETIMEDOUT;
}

/* 按功能码核对完整 ADU 长度，避免合法前缀后还有尾部字节时执行写入。
 * CRC、站号、范围和协议异常继续由 aModbus/nanoMODBUS 检查。 */
static aBool_t frame_length_valid(void)
{
    uint8_t function;
    if (frame_size < 4U) return A_FALSE;
    function = frame[1];
#if APP_MODBUS_MASTER_ENABLE
    if ((function & 0x80U) != 0U) return frame_size == 5U;
    if (function >= 1U && function <= 4U) {
        return frame_size == 5U + frame[2];
    }
    if (function == 5U || function == 6U ||
        function == 15U || function == 16U) return frame_size == 8U;
#else
    if (function >= 1U && function <= 6U) return frame_size == 8U;
    if (function == 15U || function == 16U) {
        return frame_size >= 9U && frame_size == 9U + frame[6];
    }
#endif
    return A_TRUE;
}

static aStatus_t receive_frame(aTimeout_t timeout)
{
    aTimepoint_t deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    aSSize_t count;
    uint8_t extra;

    appModbusPortFrameReset();
    for (;;) {
        aTimeout_t wait = frame_size == 0U ?
            aTimepointRemaining(&deadline, aOSGetUptimeMs()) :
            A_TIMEOUT_NO_WAIT;
        size_t space = sizeof(frame) - frame_size;
        count = aDevUsartRead(usart, space != 0U ? frame + frame_size :
                             &extra, space != 0U ? space : 1U, wait);
        if (aDevUsartHasRxOverflowed(usart)) return A_STATUS_ERROR;
        if (count > 0) {
            last_activity = aOSGetUptimeMs();
            if (space == 0U) return A_STATUS_ERROR;
            frame_size += (size_t)count;
        } else {
            if (count < 0 && !no_data()) return A_STATUS_ERROR;
            if (frame_size == 0U) return A_STATUS_TIMEOUT;
            if ((uint32_t)(aOSGetUptimeMs() - last_activity) >= quiet_ms) {
#if !APP_MODBUS_MASTER_ENABLE
                /* 不把其他站点的帧交给上游旁听逻辑，避免继续等其响应。 */
                if (frame[0] != 0U && frame[0] != APP_MODBUS_UNIT_ID) {
                    return A_STATUS_TIMEOUT;
                }
#endif
                if (!frame_length_valid()) return A_STATUS_ERROR;
                frame_ready = A_TRUE;
                return A_STATUS_OK;
            }
        }
        if (aTimepointExpired(&deadline, aOSGetUptimeMs())) {
            /* 半帧超时必须报告错误，触发协议层下一轮恢复。 */
            return A_STATUS_ERROR;
        }
        if (count <= 0) aOSDelayMs(1U);
    }
}

static aSSize_t port_read(void *context, void *data, size_t size,
                          aTimeout_t timeout)
{
    aStatus_t status;
    size_t available;
    (void)context;
    if (size == 0U) return 0;
    if (!frame_ready) {
        status = receive_frame(timeout);
        if (status != A_STATUS_OK) return aOSFailWithStatus(status);
    }
    available = frame_size - frame_position;
    if (size > available) size = available;
    memcpy(data, frame + frame_position, size);
    frame_position += size;
    return (aSSize_t)size;
}

static aSSize_t port_write(void *context, const void *data, size_t size,
                           aTimeout_t timeout)
{
    (void)context;
    return aDevUsartWrite(usart, data, size, timeout);
}

static aStatus_t port_wait(void *context, aTimeout_t timeout)
{
    aStatus_t status;
    (void)context;
    status = aDevUsartWaitTransmitComplete(usart, timeout);
    if (status == A_STATUS_OK) last_activity = aOSGetUptimeMs();
    return status;
}

static aStatus_t port_prepare(void *context, aTimeout_t timeout)
{
    aTimepoint_t deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    aStatus_t status;
    (void)context;
    /* 上一笔超时也可能仍在发送，必须先等 TC 和 DE 释放。 */
    status = port_wait(NULL, timeout);
    if (status != A_STATUS_OK) return status;
    while ((uint32_t)(aOSGetUptimeMs() - last_activity) < quiet_ms) {
        if (aTimepointExpired(&deadline, aOSGetUptimeMs())) {
            return A_STATUS_TIMEOUT;
        }
        aOSDelayMs(1U);
    }
    return A_STATUS_OK;
}

static aStatus_t port_discard(void *context, aTimeout_t timeout)
{
    aTimepoint_t deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    uint8_t discarded[32U];
    aSSize_t count;
    (void)context;
    appModbusPortFrameReset();
    last_activity = aOSGetUptimeMs();
    for (;;) {
        count = aDevUsartRead(usart, discarded, sizeof(discarded),
                              A_TIMEOUT_NO_WAIT);
        if (count > 0) last_activity = aOSGetUptimeMs();
        else {
            if (count < 0 && !no_data()) return A_STATUS_ERROR;
            if ((uint32_t)(aOSGetUptimeMs() - last_activity) >= quiet_ms) {
                aDevUsartClearRxOverflow(usart);
                return A_STATUS_OK;
            }
        }
        if (aTimepointExpired(&deadline, aOSGetUptimeMs())) {
            return A_STATUS_TIMEOUT;
        }
        if (count <= 0) aOSDelayMs(1U);
    }
}

aStatus_t appModbusPortInit(aModbusTransport_t *transport)
{
    aStatus_t status;
    if (transport == NULL) return A_STATUS_INVALID_PARAM;
    if (usart != NULL) return A_STATUS_BUSY;
    aModbusTransportStructInit(transport);
#if ADEV_USART_DYNAMIC_ENABLE
    status = aDevUsartCreate(&usart_config, &usart);
#else
    status = aDevUsartInitStatic(&usart_config, &usart_instance);
    if (status == A_STATUS_OK) usart = &usart_instance;
#endif
    if (status != A_STATUS_OK) return status;
    appModbusPortFrameReset();
    last_activity = aOSGetUptimeMs();
    transport->read = port_read;
    transport->write = port_write;
    transport->discard_input = port_discard;
    transport->prepare_frame = port_prepare;
    transport->wait_transmit_complete = port_wait;
    return A_STATUS_OK;
}

aStatus_t appModbusPortDeInit(void)
{
    aStatus_t status;
    if (usart == NULL) return A_STATUS_NOT_READY;
#if ADEV_USART_DYNAMIC_ENABLE
    status = aDevUsartDestroy(usart);
#else
    status = aDevUsartDeInit(usart);
#endif
    if (status == A_STATUS_OK) {
        usart = NULL;
        appModbusPortFrameReset();
    }
    return status;
}
