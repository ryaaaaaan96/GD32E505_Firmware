#include "rs485_config.h"

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

void appRs485ConfigInit(aDevUsartConfig_t *config)
{
    if (config != NULL) *config = usart_config;
}
