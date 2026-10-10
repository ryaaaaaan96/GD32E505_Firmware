#include "system_device.h"
#include "aDev_led_instance.h"
#if ASHELL_ENABLE
#include "aDev_usart.h"
#include "aOS.h"
#include "aShell.h"
#endif

/* --------------------------------------------------------------------------
 * 系统状态指示灯：PA8，低电平点亮。
 * -------------------------------------------------------------------------- */
static const aDevLedConfig_t led_config = {
    .pin = ADRV_PIN(ADRV_GPIO_PORT_A, 8),
    .active_level = ADEV_LED_ACTIVE_LOW,
    .speed = ADRV_GPIO_SPEED_LOW,
    .initially_on = A_FALSE,
};

static aDevLedHandle_t led_handle;

aStatus_t appSystemStatusLedInit(aDevLedHandle_t **handle_out)
{
    aStatus_t status;

    if (handle_out == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    *handle_out = NULL;
    status = aDevLedInitStatic(&led_config, &led_handle);
    if (status != A_STATUS_OK) {
        return status;
    }
    *handle_out = &led_handle;
    return A_STATUS_OK;
}

/* --------------------------------------------------------------------------
 * 系统控制台：USART0，随 Shell 功能一同裁剪。
 * -------------------------------------------------------------------------- */
#if ASHELL_ENABLE

/* 控制台配置、缓冲区和句柄由本文件私有持有。
 * appSystemConsoleInit 完成控制台及 Shell 单例初始化。 */
static uint8_t rx_buffer[256U];
static uint8_t tx_buffer[256U];

static const aDevUsartConfig_t usart_config = {
    .drv_config = {
        .id = ADRV_USART_0,
        .baud_rate = 115200U,
        .parity = ADRV_USART_PARITY_NONE,
        .stop_bits = ADRV_USART_STOP_1,
        .tx_pin = ADRV_PIN(ADRV_GPIO_PORT_A, 9),
        .rx_pin = ADRV_PIN(ADRV_GPIO_PORT_A, 10),
    },
    .mode = ADEV_USART_TX_INTERRUPT_BUFFERED |
            ADEV_USART_RX_INTERRUPT_BUFFERED |
            ADEV_USART_OPTION_RX_IDLE,
    .interrupt_priority = 6U,
    .rx_buffer = rx_buffer,
    .rx_buffer_size = sizeof(rx_buffer),
    .tx_buffer = tx_buffer,
    .tx_buffer_size = sizeof(tx_buffer),
    .rs485 = {
        .mode = ADEV_USART_RS485_NONE,
        .de_pin = ADRV_PIN_NONE,
        .de_active_level = ADRV_GPIO_HIGH,
    },
};

/* RX 由 Shell 独占；TX 由 Shell 和日志共享，只在统一发送入口加锁。 */
static aDevUsartHandle_t *console_handle;
static aOSMutex_t console_tx_mutex;

/* 应用适配：通用流不暴露 USART 类型；接收故障先报告给 Shell 再恢复。 */
static aSSize_t console_read(void *buffer, size_t size,
                            aTimeout_t timeout)
{
    aSSize_t count = aDevUsartRead(console_handle, buffer, size, timeout);
    if (count < 0 && aDevUsartGetRxError(console_handle) != A_STATUS_OK)
        aDevUsartClearRxError(console_handle);
    return count;
}

static aSSize_t console_write(const void *buffer, size_t size,
                             aTimeout_t timeout)
{
    aTimepoint_t deadline;
    aTimeout_t remaining;
    aStatus_t status;
    aSSize_t count;

    if (!aTimeoutIsValid(timeout) || size > (size_t)PTRDIFF_MAX ||
        (buffer == NULL && size != 0U)) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (console_handle == NULL) {
        return aOSFailWithStatus(A_STATUS_NOT_READY);
    }
    if (size == 0U) return 0;

    deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(console_tx_mutex, timeout);
    if (status != A_STATUS_OK) return aOSFailWithStatus(status);

    /* 锁等待与串口写入共用预算；NO_WAIT 仍允许尝试立即提交。 */
    remaining = aTimepointRemaining(&deadline, aOSGetUptimeMs());
    if (timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
        timeout.milliseconds != 0U && remaining.milliseconds == 0U) {
        count = aOSFailWithStatus(A_STATUS_TIMEOUT);
    } else {
        count = aDevUsartWrite(
            console_handle, buffer, size, remaining);
    }
    status = aOSMutexUnlock(console_tx_mutex);
    /* 已提交的字节必须返回实际进度，不能因解锁错误诱发整段重发。 */
    if (count == 0 && status != A_STATUS_OK) {
        return aOSFailWithStatus(status);
    }
    return count;
}

const aStream_t app_system_console_stream = {
    .read = console_read,
    .write = console_write,
    .flush = NULL, /* write 已提交发送；不等同于线路发送完成。 */
};

aStatus_t appSystemConsoleInit(void)
{
    aShellConfig_t config;
    aStatus_t status;
    aStatus_t cleanup_status;

    /* 保留仍在使用或尚未清理成功的句柄，避免 Create 覆盖所有权。 */
    if (console_handle != NULL) return A_STATUS_BUSY;
    aShellConfigStructInit(&config);
    status = aOSMutexCreate(&console_tx_mutex);
    if (status != A_STATUS_OK) return status;
    status = aDevUsartCreate(
        &usart_config,
        &console_handle);
    if (status != A_STATUS_OK) {
        aOSMutexDestroy(&console_tx_mutex);
        return status;
    }
    config.stream = app_system_console_stream;
    config.read_timeout = A_TIMEOUT_MS(20U);
    config.write_timeout = A_TIMEOUT_MS(20U);

    status = aShellInit(&config);
    if (status != A_STATUS_OK) {
        cleanup_status = aDevUsartDestroy(console_handle);
        if (cleanup_status != A_STATUS_OK) return cleanup_status;
        console_handle = NULL;
        aOSMutexDestroy(&console_tx_mutex);
        return status;
    }
    return A_STATUS_OK;
}

aStatus_t appSystemConsoleDeInit(void)
{
    aStatus_t status;

    if (console_handle == NULL) return A_STATUS_NOT_READY;
    /* 调用者已停止读写；串口仍忙时保留句柄与 Shell，允许稍后重试清理。 */
    status = aDevUsartDestroy(console_handle);
    if (status != A_STATUS_OK) return status;
    console_handle = NULL;
    aOSMutexDestroy(&console_tx_mutex);
    status = aShellDeInit();
    /* Shell 初始化失败后，也允许通过此入口清理遗留串口。 */
    return status == A_STATUS_NOT_READY ? A_STATUS_OK : status;
}

#endif
