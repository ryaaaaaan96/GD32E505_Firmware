#include "app_system_device.h"

#define APP_STATUS_LED_PIN ADRV_PIN(ADRV_GPIO_PORT_A, 8)
#define APP_STATUS_LED_ACTIVE_LEVEL ADEV_LED_ACTIVE_LOW

enum { INSTANCE_COLD, INSTANCE_STARTING, INSTANCE_DONE };

/* System status indicator. */
static const aDevLedConfig_t led_config = {
    .pin = APP_STATUS_LED_PIN,
    .active_level = APP_STATUS_LED_ACTIVE_LEVEL,
    .initially_on = A_FALSE,
};

static aDevLedHandle_t led_handle;
static unsigned led_phase;
static aStatus_t led_init_result = A_STATUS_NOT_READY;

aStatus_t appSystemStatusLedInit(appLedId_t id, aDevLedHandle_t **handle_out)
{
    if (handle_out == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    *handle_out = NULL;
    switch (id) {
    case APP_LED_STATUS:
        if (led_phase == INSTANCE_STARTING) {
            return A_STATUS_BUSY;
        }
        if (led_phase == INSTANCE_COLD) {
            led_phase = INSTANCE_STARTING;
            led_init_result = aDevLedInit(&led_config, &led_handle);
            led_phase = INSTANCE_DONE;
        }
        if (led_init_result != A_STATUS_OK) {
            return led_init_result;
        }
        *handle_out = &led_handle;
        return A_STATUS_OK;
    default:
        return A_STATUS_NOT_FOUND;
    }
}

/* System console transport. */
#if ASHELL_ENABLED

/* Console settings are private to this device instance. */
#define APP_CONSOLE_RX_BUFFER_SIZE 256U
#define APP_CONSOLE_TX_BUFFER_SIZE 256U
#define APP_CONSOLE_USART_MODE                  \
    (ADEV_USART_TX_INTERRUPT_BUFFERED |         \
     ADEV_USART_RX_INTERRUPT_BUFFERED |         \
     ADEV_USART_OPTION_RX_IDLE)

/* Product resources are private to this file. The business-facing identity is
 * APP_USART_CONSOLE; no public global usart_handle or device-specific getter exists. */
static uint8_t rx_buffer[APP_CONSOLE_RX_BUFFER_SIZE];
static uint8_t tx_buffer[APP_CONSOLE_TX_BUFFER_SIZE];

static const aDevUsartConfig_t usart_config = {
    .drv_config = {
        .id = ADRV_USART_0,
        .baud_rate = 115200U,
        .parity = ADRV_USART_PARITY_NONE,
        .stop_bits = ADRV_USART_STOP_1,
        .tx_pin = ADRV_PIN(ADRV_GPIO_PORT_A, 9),
        .rx_pin = ADRV_PIN(ADRV_GPIO_PORT_A, 10),
    },
    .mode = APP_CONSOLE_USART_MODE,
    .interrupt_priority = 6U,
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

static aDevUsartStorage_t usart_storage;
static aDevUsartHandle_t *usart_handle;
static unsigned usart_phase;
static aStatus_t usart_init_result = A_STATUS_NOT_READY;

aStatus_t appSystemConsoleInit(appUsartId_t id, aDevUsartHandle_t **handle_out)
{
    if (handle_out == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    *handle_out = NULL;
    switch (id) {
    case APP_USART_CONSOLE:
        if (usart_phase == INSTANCE_STARTING) {
            return A_STATUS_BUSY;
        }
        if (usart_phase == INSTANCE_COLD) {
            usart_phase = INSTANCE_STARTING;
            usart_init_result = aDevUsartInitStatic(&usart_config, &usart_storage, &usart_handle);
            usart_phase = INSTANCE_DONE;
        }
        if (usart_init_result != A_STATUS_OK) {
            return usart_init_result;
        }
        *handle_out = usart_handle;
        return A_STATUS_OK;
    default:
        return A_STATUS_NOT_FOUND;
    }
}

#endif
