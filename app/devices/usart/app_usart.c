#include "aclass_system_config.h"
#include "app_usart.h"

#if ASHELL_ENABLED

/* Product resources are private to this file. The business-facing identity is
 * APP_USART_CONSOLE; no public global handle or device-specific getter exists. */
static uint8_t rx_buffer[ASYSTEM_SHELL_RX_BUFFER_SIZE];
static uint8_t tx_buffer[ASYSTEM_SHELL_TX_BUFFER_SIZE];

static const aDevUsartConfig_t config = {
    .drv_config = {
        .id = ASYSTEM_SHELL_USART,
        .baud_rate = ASYSTEM_SHELL_BAUD_RATE,
        .parity = ADRV_USART_PARITY_NONE,
        .stop_bits = ADRV_USART_STOP_1,
        .tx_pin = ASYSTEM_SHELL_TX_PIN,
        .rx_pin = ASYSTEM_SHELL_RX_PIN,
    },
    .mode = ASYSTEM_SHELL_USART_MODE,
    .interrupt_priority = ASYSTEM_SHELL_IRQ_PRIORITY,
    .rx_buffer = rx_buffer,
    .rx_buffer_size = sizeof(rx_buffer),
    .tx_buffer = tx_buffer,
    .tx_buffer_size = sizeof(tx_buffer),
    .rs485 = {
        .enabled = A_FALSE,
        .de_pin = ADRV_PIN_NONE,
        .re_pin = ADRV_PIN_NONE,
        .de_active_level = ADRV_GPIO_HIGH,
        .re_active_level = ADRV_GPIO_LOW,
        .receive_during_tx = A_FALSE,
    },
};

static aDevUsartStorage_t storage;
static aDevUsartHandle_t *handle;
enum { INSTANCE_COLD, INSTANCE_STARTING, INSTANCE_DONE };
static unsigned phase;
static aStatus_t init_result = A_STATUS_NOT_READY;
#endif

aStatus_t appUsartInit(appUsartId_t id, aDevUsartHandle_t **handle_out)
{
    if (handle_out == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    *handle_out = NULL;
    switch (id) {
#if ASHELL_ENABLED
    case APP_USART_CONSOLE:
        if (phase == INSTANCE_STARTING) {
            return A_STATUS_BUSY;
        }
        if (phase == INSTANCE_COLD) {
            phase = INSTANCE_STARTING;
            init_result = aDevUsartInitStatic(&config, &storage, &handle);
            phase = INSTANCE_DONE;
        }
        if (init_result != A_STATUS_OK) {
            return init_result;
        }
        *handle_out = handle;
        return A_STATUS_OK;
#endif
    default:
        return A_STATUS_NOT_FOUND;
    }
}
