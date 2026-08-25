#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

#include "aOS.h"
#include "aDrv.h"
#include "aDrv_gpio.h"
#include "aDev_usart.h"
#include "aModbus.h"
#include "aShell.h"

#define APP_RUN_LED_PIN ADRV_PIN(ADRV_GPIO_PORT_A, 8)
#define APP_CONSOLE_TX_PIN ADRV_PIN(ADRV_GPIO_PORT_A, 9)
#define APP_CONSOLE_RX_PIN ADRV_PIN(ADRV_GPIO_PORT_A, 10)
#define APP_CONSOLE_TIMEOUT_MS 100U

static aDevHandle_Usart_t s_console;
static aShellHandle_t s_shell;

static void appFatal(void)
{
    for (;;) {
    }
}

static int32_t appConsoleWrite(const void *data, uint16_t size)
{
    return aDevUsartWrite(&s_console, data, size, APP_CONSOLE_TIMEOUT_MS);
}

static void appConsolePrintf(const char *format, ...)
{
    char buffer[192];
    va_list arguments;
    va_start(arguments, format);
    const int count = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (count <= 0) return;
    const size_t length = (size_t)count < sizeof(buffer) ?
                          (size_t)count : sizeof(buffer) - 1U;
    (void)appConsoleWrite(buffer, (uint16_t)length);
}

static int16_t appShellWrite(char *data, uint16_t size)
{
    const int32_t result = appConsoleWrite(data, size);
    return result < 0 ? (int16_t)-1 : (int16_t)result;
}

static int16_t appShellRead(char *data, uint16_t size)
{
    const int32_t result = aDevUsartRead(&s_console, data, size, 20U);
    return result < 0 ? (int16_t)-1 : (int16_t)result;
}

static aStatus_t appGpioInit(void)
{
    aDrvGpioConfig_t config;
    aDrvGpioConfigStructInit(&config);
    config.pin = APP_RUN_LED_PIN;
    config.mode = ADRV_GPIO_OUTPUT_PUSH_PULL;
    config.initial_level = ADRV_GPIO_HIGH;
    return aDrvGpioInit(&config);
}

static aStatus_t appConsoleInit(void)
{
    aDevConfig_Usart_t config;
    aDevUsartConfigStructInit(&config);
    config.drv_config.id = ADRV_USART_1;
    config.drv_config.tx_pin = APP_CONSOLE_TX_PIN;
    config.drv_config.rx_pin = APP_CONSOLE_RX_PIN;
    config.drv_config.baud_rate = 115200U;
    aDevUsartHandleStructInit(&s_console);
    return aDevUsartInit(&config, &s_console);
}

static void appReport(const char *name, int passed)
{
    appConsolePrintf("[SELF-TEST] %s: %s\r\n", name,
                     passed != 0 ? "PASS" : "FAIL");
}

static int appModbusTest(void)
{
    static const uint8_t input[] = "123456789";
    return aModbusCrc16(input, sizeof(input) - 1U) == 0x4B37U;
}

static int appShellInit(void)
{
    aShellConfig_t config;
    aShellConfigStructInit(&config);
    aShellHandleStructInit(&s_shell);
    config.read = appShellRead;
    config.write = appShellWrite;
    config.buffer_size = 256U;
    config.task_stack_size = 512U;
    config.task_priority = AOS_TASK_PRIO_LOW;
    if (aShellInit(&s_shell, &config) != A_STATUS_OK) return 0;
    aShellPrint(&s_shell, "[SELF-TEST] shell output: PASS\r\n");
    return 1;
}

static void appTestTask(void *argument)
{
    (void)argument;
    const aDrvCapabilities_t *capabilities = aDrvGetCapabilities();
    appConsolePrintf("\r\nAClass IO firmware: %s ready\r\n",
                     capabilities->mcu_name);

    appReport("USART device", 1);
    appReport("GPIO LED", 1);
    appReport("Modbus CRC", appModbusTest());
    appReport("Shell", appShellInit());

    appConsolePrintf(
        "[SELF-TEST] RS485/Flash25Q/FlashDB: SKIP "
        "(application pins/devices not configured)\r\n");

    for (;;) {
        (void)aDrvGpioToggle(APP_RUN_LED_PIN);
        aOSDelayMs(500U);
    }
}

int main(void)
{
    if (aDrvInit() != A_STATUS_OK) appFatal();
    aOSInit();
    if (appGpioInit() != A_STATUS_OK) appFatal();
    if (appConsoleInit() != A_STATUS_OK) appFatal();
    if (aOSCreateTask(appTestTask, "app_test", 1024U, NULL,
                      AOS_TASK_PRIO_HIGH, NULL) != A_STATUS_OK) appFatal();
    aOSRun();
}
