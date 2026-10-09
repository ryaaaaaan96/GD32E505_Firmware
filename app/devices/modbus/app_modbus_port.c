#include "app_modbus_port.h"
#include "app_modbus.h"
#include "aDev_usart.h"
#include "aOS.h"
#include "aDrv_basic.h"
#if !ADEV_USART_DYNAMIC_ENABLE
#include "aDev_usart_instance.h"
#endif
#include <string.h>

static aDevUsartHandle_t *usart;
#if !ADEV_USART_DYNAMIC_ENABLE
static aDevUsartHandle_t usart_instance;
#endif
/* ISR 生产完整帧，任务复制后才释放槽位；满队列丢弃新帧。 */
typedef struct {
    uint8_t data[256U];
    size_t size;
} frame_slot_t;
static frame_slot_t frames[3U];
static volatile size_t frame_head, frame_tail, frame_count;
static size_t building;
static aBool_t invalid_frame;
static volatile uint32_t last_rx_cycles, last_rx_ms;
static uint32_t cycles_per_us;
static uint32_t last_tx_cycles, last_tx_ms;
static aBool_t tx_pending;
static volatile unsigned dropped_frames;
static void receive_byte(void *context, uint8_t byte, aStatus_t status);
static uint8_t tx_buffer[256U];
static uint8_t frame[256U];
static size_t frame_size;
static size_t frame_position;
static aBool_t frame_ready;


/* 固定 115200：帧内间隔 750 us，帧间静默 1750 us。
 * 周期计数用于短间隔；毫秒时基区分长空闲，避免周期计数回绕误判。 */
static const uint32_t frame_gap_us = 1750U;
/* RXNE 时间戳落在字符末尾；比较相邻完成时刻时加上 8N1 的 10 位。
 * 向上取整为 87 us，任务封帧也等待这一余量，避免提前截断在途字符。 */
static const uint32_t character_us = 87U;
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
            ADEV_USART_RX_INTERRUPT_CALLBACK,
    .interrupt_priority = 6U,
    .rx_byte_callback = receive_byte,
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

static aBool_t elapsed(uint32_t cycles, uint32_t ms, uint32_t us)
{
    if ((uint32_t)(aOSGetUptimeMs() - ms) > 10U) return A_TRUE;
    return (uint32_t)(aDrvCycleCounterRead() - cycles) >=
           us * cycles_per_us;
}

/* 调用者是 USART ISR，或已经屏蔽该 ISR 的任务。 */
static void finish_frame(void)
{
    if (building != 0U && !invalid_frame) {
        frames[frame_head].size = building;
        frame_head = (frame_head + 1U) % 3U;
        frame_count++;
    } else if (invalid_frame) {
        dropped_frames++;
    }
    building = 0U;
    invalid_frame = A_FALSE;
}

static void receive_byte(void *context, uint8_t byte, aStatus_t status)
{
    (void)context;
    if (elapsed(last_rx_cycles, last_rx_ms, frame_gap_us + character_us))
        finish_frame();
    else if (building != 0U &&
             elapsed(last_rx_cycles, last_rx_ms, 750U + character_us))
        invalid_frame = A_TRUE;
    last_rx_cycles = aDrvCycleCounterRead();
    last_rx_ms = aOSGetUptimeMs();
    if (status != A_STATUS_OK || frame_count == 3U ||
        building == sizeof(frames[0].data)) invalid_frame = A_TRUE;
    if (!invalid_frame) frames[frame_head].data[building++] = byte;
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

    appModbusPortFrameReset();
    for (;;) {
        size_t available;
        size_t tail;
        aOSCriticalEnter();
        if (elapsed(last_rx_cycles, last_rx_ms, frame_gap_us + character_us))
            finish_frame();
        available = frame_count;
        tail = frame_tail;
        aOSCriticalExit();
        if (available != 0U) {
            /* 未归还槽位前 ISR 不会覆盖它；复制不需要关中断。 */
            frame_size = frames[tail].size;
            memcpy(frame, frames[tail].data, frame_size);
            aOSCriticalEnter();
            frame_tail = (tail + 1U) % 3U;
            frame_count--;
            aOSCriticalExit();
#if !APP_MODBUS_MASTER_ENABLE
            if (frame[0] != 0U && frame[0] != APP_MODBUS_UNIT_ID) continue;
#endif
            if (!frame_length_valid()) return A_STATUS_ERROR;
            frame_ready = A_TRUE;
            return A_STATUS_OK;
        }
        if (aTimepointExpired(&deadline, aOSGetUptimeMs()))
            return A_STATUS_TIMEOUT;
        aOSDelayMs(1U);
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
    aSSize_t count = aDevUsartWrite(usart, data, size, timeout);
    if (count > 0) {
        tx_pending = A_TRUE;
        last_tx_cycles = aDrvCycleCounterRead();
        last_tx_ms = aOSGetUptimeMs();
    }
    return count;
}

static aStatus_t port_wait(void *context, aTimeout_t timeout)
{
    aStatus_t status;
    (void)context;
    status = aDevUsartWaitTransmitComplete(usart, timeout);
    if (status == A_STATUS_OK && tx_pending) {
        last_tx_cycles = aDrvCycleCounterRead();
        last_tx_ms = aOSGetUptimeMs();
        tx_pending = A_FALSE;
    }
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
    for (;;) {
        aBool_t quiet;
        aOSCriticalEnter();
        quiet = elapsed(last_rx_cycles, last_rx_ms, frame_gap_us);
        aOSCriticalExit();
        if (quiet && elapsed(last_tx_cycles, last_tx_ms, frame_gap_us))
            break;
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
    (void)context;
    appModbusPortFrameReset();
    for (;;) {
        aBool_t quiet;
        aOSCriticalEnter();
        frame_head = frame_tail = frame_count = 0U;
        building = 0U;
        quiet = elapsed(last_rx_cycles, last_rx_ms, frame_gap_us);
        invalid_frame = !quiet;
        aOSCriticalExit();
        if (quiet) {
            aDevUsartClearRxError(usart);
            return A_STATUS_OK;
        }
        if (aTimepointExpired(&deadline, aOSGetUptimeMs()))
            return A_STATUS_TIMEOUT;
        aOSDelayMs(1U);
    }
}

aStatus_t appModbusPortInit(aModbusTransport_t *transport)
{
    aStatus_t status;
    if (transport == NULL) return A_STATUS_INVALID_PARAM;
    if (usart != NULL) return A_STATUS_BUSY;
    status = aDrvCycleCounterEnable();
    if (status != A_STATUS_OK) return status;
    cycles_per_us = aDrvGetCoreClockHz() / 1000000U;
    if (cycles_per_us == 0U ||
        cycles_per_us > UINT32_MAX / (frame_gap_us + character_us))
        return A_STATUS_UNSUPPORTED;
    frame_head = frame_tail = frame_count = building = 0U;
    invalid_frame = tx_pending = A_FALSE;
    dropped_frames = 0U;
    last_rx_cycles = last_tx_cycles = aDrvCycleCounterRead();
    last_rx_ms = last_tx_ms = aOSGetUptimeMs();
    aModbusTransportStructInit(transport);
#if ADEV_USART_DYNAMIC_ENABLE
    status = aDevUsartCreate(&usart_config, &usart);
#else
    status = aDevUsartInitStatic(&usart_config, &usart_instance);
    if (status == A_STATUS_OK) usart = &usart_instance;
#endif
    if (status != A_STATUS_OK) return status;
    appModbusPortFrameReset();

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
