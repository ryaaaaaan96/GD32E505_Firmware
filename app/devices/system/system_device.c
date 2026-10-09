#include "system_device.h"
#if ASHELL_ENABLE
#include "aDev_usart.h"
#include "aShell.h"
#endif

/* 系统状态指示灯实例。 */
static const aDevLedConfig_t led_config = {
    .pin = ADRV_PIN(ADRV_GPIO_PORT_A, 8),
    .active_level = ADEV_LED_ACTIVE_LOW,
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
    status = aDevLedInit(&led_config, &led_handle);
    if (status != A_STATUS_OK) {
        return status;
    }
    *handle_out = &led_handle;
    return A_STATUS_OK;
}

/* 系统控制台串口实例，随 Shell 功能一同裁剪。 */
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

static aDevUsartHandle_t *console_handle;

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
    return aDevUsartWrite(console_handle, buffer, size, timeout);
}

aStatus_t appSystemConsoleInit(void)
{
    aShellConfig_t config;
    aStatus_t status;

    aShellConfigStructInit(&config);
    status = aDevUsartCreate(
        &usart_config,
        &console_handle);
    if (status != A_STATUS_OK) {
        return status;
    }
    config.stream.read = console_read;
    config.stream.write = console_write;
    config.stream.flush = NULL; /* Write already submits output to USART. */
    config.read_timeout = A_TIMEOUT_MS(20U);
    config.write_timeout = A_TIMEOUT_MS(20U);

    status = aShellInit(&config);
    if (status != A_STATUS_OK) {
        (void)aDevUsartDestroy(console_handle);
        console_handle = NULL;
        return status;
    }
    return A_STATUS_OK;
}

#endif
