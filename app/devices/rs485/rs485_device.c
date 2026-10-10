#include "rs485_device.h"
#include "aDev_usart.h"
#include "aDrv_basic.h"
#include "aOS.h"
#if !ADEV_USART_DYNAMIC_ENABLE
#include "aDev_usart_instance.h"
#endif

/* 设备层只配置串口、缓冲区和方向引脚，不处理协议帧。 */
static uint8_t tx_buffer[256U];
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
    .tx_buffer = tx_buffer,
    .tx_buffer_size = sizeof(tx_buffer),
    .rs485 = {
        .mode = ADEV_USART_RS485_GPIO_DE,
        .de_pin = ADRV_PIN(ADRV_GPIO_PORT_A, 15),
        .de_active_level = ADRV_GPIO_HIGH,
    },
};

/* 仅持有物理串口；协议实例及接收上下文由调用方管理。 */
/* 发送由协议任务独占，接收在 ISR 交付；打开/关闭须与协议任务串行化。 */
static aDevUsartHandle_t *usart;
#if !ADEV_USART_DYNAMIC_ENABLE
static aDevUsartHandle_t usart_instance;
#endif

static aSSize_t writeBytes(const void *data, size_t size, aTimeout_t timeout)
{
    if (usart == NULL) return aOSFailWithStatus(A_STATUS_NOT_READY);
    return aDevUsartWrite(usart, data, size, timeout);
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

static aStatus_t waitTransmit(void *context, aTimeout_t timeout)
{
    (void)context;
    if (usart == NULL) return A_STATUS_NOT_READY;
    return aDevUsartWaitTransmitComplete(usart, timeout);
}

static void clearError(void *context)
{
    (void)context;
    if (usart != NULL) aDevUsartClearRxError(usart);
}

aStatus_t rs485PortPrepare(rs485Port_t *port)
{
    const aDrvUsartConfig_t *config = &usart_config.drv_config;
    aStatus_t status;

    if (port == NULL) return A_STATUS_INVALID_PARAM;
    if (usart != NULL) return A_STATUS_BUSY;
    if ((usart_config.mode & ADEV_USART_RX_MASK) !=
            ADEV_USART_RX_INTERRUPT_CALLBACK ||
        (config->stop_bits != ADRV_USART_STOP_1 &&
         config->stop_bits != ADRV_USART_STOP_2) ||
        (config->parity != ADRV_USART_PARITY_NONE &&
         config->parity != ADRV_USART_PARITY_EVEN &&
         config->parity != ADRV_USART_PARITY_ODD)) {
        return A_STATUS_INVALID_PARAM;
    }
    status = aDrvCycleCounterEnable();
    if (status != A_STATUS_OK) return status;
    aStreamStructInit(&port->output);
    port->output.write = writeBytes;
    port->baud_rate = config->baud_rate;
    port->character_bits = 10U +
        (config->parity != ADRV_USART_PARITY_NONE) +
        (config->stop_bits == ADRV_USART_STOP_2);
    port->ticks_per_second = aDrvGetCoreClockHz();
    port->ticks = ticks;
    port->enter = enter;
    port->exit = leave;
    port->wait_transmit_complete = waitTransmit;
    port->clear_error = clearError;
    return A_STATUS_OK;
}

aStatus_t rs485PortOpen(rs485ReceiveFn_t receive, void *context)
{
    aDevUsartConfig_t config = usart_config;
#if !ADEV_USART_DYNAMIC_ENABLE
    aStatus_t status;
#endif

    if (receive == NULL) return A_STATUS_INVALID_PARAM;
    if (usart != NULL) return A_STATUS_BUSY;
    config.rx_byte_callback = receive;
    config.rx_byte_context = context;
#if ADEV_USART_DYNAMIC_ENABLE
    return aDevUsartCreate(&config, &usart);
#else
    status = aDevUsartInitStatic(&config, &usart_instance);
    if (status == A_STATUS_OK) usart = &usart_instance;
    return status;
#endif
}

aStatus_t rs485PortClose(void)
{
    aStatus_t status;

    if (usart == NULL) return A_STATUS_NOT_READY;
#if ADEV_USART_DYNAMIC_ENABLE
    status = aDevUsartDestroy(usart);
#else
    status = aDevUsartDeInit(usart);
#endif
    if (status == A_STATUS_OK) usart = NULL;
    return status;
}
