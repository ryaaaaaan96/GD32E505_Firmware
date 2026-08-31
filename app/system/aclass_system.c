#include "aclass_system.h"

#include "aDev_led.h"
#include "aOS.h"
#include "aclass_system_config.h"

#if ASYSTEM_SHELL_ENABLED
#include "aDev_usart.h"
#include "aDrv_basic.h"
#include "aShell.h"
#endif

/* --------------------------------------------------------------------------
 * Status LED
 * -------------------------------------------------------------------------- */

static aDevLedHandle_t s_status_led;

static void statusTask(void *argument)
{
    (void)argument;

    for (;;) {
        if (aDevLedToggle(&s_status_led) != A_STATUS_OK) {
            (void)aDevLedOff(&s_status_led);
            for (;;) {
                aOSDelayMs(ASYSTEM_STATUS_BLINK_PERIOD_MS);
            }
        }
        aOSDelayMs(ASYSTEM_STATUS_BLINK_PERIOD_MS);
    }
}

static aStatus_t statusInit(void)
{
    aDevLedConfig_t config;
    aStatus_t status;

    aDevLedConfigStructInit(&config);
    aDevLedHandleStructInit(&s_status_led);
    config.pin = ASYSTEM_STATUS_LED_PIN;
    config.active_level = ASYSTEM_STATUS_LED_ACTIVE_LEVEL;
    config.initially_on = false;

    status = aDevLedInit(&config, &s_status_led);
    if (status != A_STATUS_OK) {
        return status;
    }

    return aOSCreateTask(statusTask, "status", 256U, NULL,
                         AOS_TASK_PRIO_NORMAL, NULL);
}

/* --------------------------------------------------------------------------
 * Shell console
 * -------------------------------------------------------------------------- */

#if ASYSTEM_SHELL_ENABLED

static aDevUsartHandle_t s_shell_usart;
static aShellHandle_t s_shell;
static uint8_t s_shell_rx_buffer[ASYSTEM_SHELL_RX_BUFFER_SIZE];
static uint8_t s_shell_tx_buffer[ASYSTEM_SHELL_TX_BUFFER_SIZE];

static int16_t shellWrite(char *buffer, uint16_t size)
{
    const aSSize_t result = aDevUsartWrite(
        &s_shell_usart, buffer, size, ASYSTEM_SHELL_IO_TIMEOUT);

    return result < 0 ? -1 : (int16_t)result;
}

static int16_t shellRead(char *buffer, uint16_t size)
{
    const aSSize_t result = aDevUsartRead(
        &s_shell_usart, buffer, size, ASYSTEM_SHELL_IO_TIMEOUT);

    return result < 0 ? -1 : (int16_t)result;
}

static aStatus_t shellUsartInit(void)
{
    aDevUsartConfig_t config;

    aDevUsartConfigStructInit(&config);
    aDevUsartHandleStructInit(&s_shell_usart);
    config.drv_config.id = ASYSTEM_SHELL_USART;
    config.drv_config.tx_pin = ASYSTEM_SHELL_TX_PIN;
    config.drv_config.rx_pin = ASYSTEM_SHELL_RX_PIN;
    config.drv_config.baud_rate = ASYSTEM_SHELL_BAUD_RATE;
    config.mode = ASYSTEM_SHELL_USART_MODE;
    config.interrupt_priority = ASYSTEM_SHELL_IRQ_PRIORITY;
    config.rx_buffer = s_shell_rx_buffer;
    config.rx_buffer_size = sizeof(s_shell_rx_buffer);
    config.tx_buffer = s_shell_tx_buffer;
    config.tx_buffer_size = sizeof(s_shell_tx_buffer);

    return aDevUsartInit(&config, &s_shell_usart);
}

static aStatus_t shellInit(void)
{
    aShellConfig_t config;
    aStatus_t status;
    uint32_t core_clock_hz;

    status = shellUsartInit();
    if (status != A_STATUS_OK) {
        return status;
    }

    aShellConfigStructInit(&config);
    aShellHandleStructInit(&s_shell);
    config.read = shellRead;
    config.write = shellWrite;

    status = aShellInit(&s_shell, &config);
    if (status != A_STATUS_OK) {
        return status;
    }

    core_clock_hz = aDrvGetCoreClockHz();
    aShellPrint(&s_shell, "\r\nsystem clock: %lu Hz\r\n",
                (unsigned long)core_clock_hz);
    aShellPrint(&s_shell, "system peripherals initialized\r\n");

    return A_STATUS_OK;
}

#else

static aStatus_t shellInit(void)
{
    return A_STATUS_OK;
}

#endif

/* --------------------------------------------------------------------------
 * System initialization
 * -------------------------------------------------------------------------- */

aStatus_t aSystemInit(void)
{
    aStatus_t status;

    status = statusInit();
    if (status != A_STATUS_OK) {
        return status;
    }

    status = shellInit();
    if (status != A_STATUS_OK) {
        return status;
    }

    return A_STATUS_OK;
}
