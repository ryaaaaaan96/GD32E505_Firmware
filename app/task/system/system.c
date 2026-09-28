#include "system.h"
#include "status.h"
#include "aOS.h"
#include "app_config.h"
#if ASHELL_ENABLED
#include "app_system_device.h"
#include "aDrv_basic.h"
#include "aShell.h"
#endif

/* 应用层负责创建和调度任务，aShell 只负责处理输入。 */
#if ASHELL_ENABLED

static aDevUsartHandle_t *s_shell_usart;

static void shellTask(void *argument)
{
    (void)argument;
    for (;;) {
        /* 成功时继续排空输入；错误或非阻塞空读时退避。 */
        if (aShellProcess() != A_STATUS_OK) {
            aOSDelayMs(ASYSTEM_SHELL_RETRY_DELAY_MS);
        }
    }
}

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

    status = appSystemConsoleInit(APP_USART_CONSOLE, &s_shell_usart);
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

    status = aOSCreateTask(shellTask, "shell", ASYSTEM_SHELL_TASK_STACK_BYTES,
                          NULL, ASYSTEM_SHELL_TASK_PRIORITY, NULL);
    if (status != A_STATUS_OK) (void)aShellDeInit();
    return status;
}

#else

static aStatus_t shellInit(void)
{
    return A_STATUS_OK;
}

#endif

/* 基础服务编排：各功能在自己的初始化函数中完成具体配置。 */
aStatus_t aSystemInit(void)
{
    aStatus_t status = statusInit();
    if (status != A_STATUS_OK) {
        return status;
    }
    return shellInit();
}
