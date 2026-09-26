#include "system.h"
#include "status.h"
#include "aclass_system_config.h"
#if ASHELL_ENABLED
#include "app_usart.h"
#include "aDrv_basic.h"
#include "aShell.h"
#endif

/* aShell owns its worker; this file supplies the application transport. */
#if ASHELL_ENABLED

static aDevUsartHandle_t *s_shell_usart;

static int16_t shellWrite(char *buffer, uint16_t size)
{
    const aSSize_t result = aDevUsartWrite(
        s_shell_usart, buffer, size, ASYSTEM_SHELL_IO_TIMEOUT);

    return result < 0 ? -1 : (int16_t)result;
}

static int16_t shellRead(char *buffer, uint16_t size)
{
    const aSSize_t result = aDevUsartRead(
        s_shell_usart, buffer, size, ASYSTEM_SHELL_IO_TIMEOUT);

    return result < 0 ? -1 : (int16_t)result;
}

static aStatus_t shellInit(void)
{
    aShellConfig_t config;
    aStatus_t status;
    uint32_t core_clock_hz;

    status = appUsartInit(APP_USART_CONSOLE, &s_shell_usart);
    if (status != A_STATUS_OK) {
        return status;
    }

    aShellConfigStructInit(&config);
    config.read = shellRead;
    config.write = shellWrite;

    status = aShellInit(&config);
    if (status != A_STATUS_OK) {
        return status;
    }

    core_clock_hz = aDrvGetCoreClockHz();
    aShellPrint("\r\nsystem clock: %lu Hz\r\n",
                (unsigned long)core_clock_hz);
    aShellPrint("system peripherals initialized\r\n");

    return A_STATUS_OK;
}

#else

static aStatus_t shellInit(void)
{
    return A_STATUS_OK;
}

#endif

/* Basic services: each function owns its initialization details. */
aStatus_t aSystemInit(void)
{
    aStatus_t status = statusInit();
    if (status != A_STATUS_OK) {
        return status;
    }
    return shellInit();
}
