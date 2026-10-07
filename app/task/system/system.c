#include "system.h"
#include "aOS.h"
#include "app_system_device.h"
#if ADEV_FLASH25Q_ENABLE
#include "app_system_flash.h"
#endif
#if APP_DATABASE_ENABLE
#include "app_database.h"
#endif
#if ABUS_ENABLE
#include "app_sig.h"
#include "app_sig_task.h"
#endif
#if ASHELL_ENABLE
#include "aDrv_basic.h"
#include "aShell.h"
#endif

static const uint32_t status_blink_period_ms = 500U;

static void statusTask(void *argument)
{
    aDevLedHandle_t *led = argument;
    for (;;) {
        if (aDevLedToggle(led) != A_STATUS_OK) {
            (void)aDevLedOff(led);
            for (;;) {
                aOSDelayMs(status_blink_period_ms);
            }
        }
        aOSDelayMs(status_blink_period_ms);
    }
}

static aStatus_t statusInit(void)
{
    aOSTaskConfig_t task_config;
    aDevLedHandle_t *led = NULL;
    aStatus_t status;

    status = appSystemStatusLedInit(&led);
    if (status != A_STATUS_OK) {
        return status;
    }
    aOSTaskConfigStructInit(&task_config);
    task_config.name = "status";
    task_config.function = statusTask;
    task_config.stack_bytes = 1024U;
    task_config.priority = AOS_TASK_PRIO_NORMAL;
    task_config.argument = led;
    status = aOSCreateTask(&task_config, NULL);
    if (status != A_STATUS_OK) {
        return status;
    }

    return A_STATUS_OK;
}

/* 应用任务统一驱动 Shell 输入处理和输出队列发送。 */
#if ASHELL_ENABLE

static void shellTask(void *argument)
{
    (void)argument;
    for (;;) {
        /* Process 同时消费输出；无输入或 I/O 错误时短暂退避。 */
        if (aShellProcess() != A_STATUS_OK) {
            aOSDelayMs(1U);
        }
    }
}

static aStatus_t shellInit(void)
{
    aOSTaskConfig_t task_config;
    aStatus_t status;
    uint32_t core_clock_hz;

    status = appSystemConsoleInit();
    if (status != A_STATUS_OK) {
        return status;
    }

    core_clock_hz = aDrvGetCoreClockHz();
    ASHELL_PRINT("\r\nsystem clock: %lu Hz\r\n",
                (unsigned long)core_clock_hz);
    ASHELL_PRINT("system peripherals initialized\r\n");

    aOSTaskConfigStructInit(&task_config);
    task_config.name = "shell";
    task_config.function = shellTask;
    task_config.stack_bytes = 2048U;
    task_config.priority = AOS_TASK_PRIO_LOW;
    status = aOSCreateTask(&task_config, NULL);
    if (status != A_STATUS_OK) {
        (void)aShellDeInit();
        return status;
    }

    return A_STATUS_OK;
}

#endif

/* 基础服务编排：各功能在自己的初始化函数中完成具体配置。 */
aStatus_t aSystemInit(void)
{
    aStatus_t status;

    status = statusInit();
    if (status != A_STATUS_OK) {
        return status;
    }

#if ASHELL_ENABLE
    status = shellInit();
    if (status != A_STATUS_OK) {
        return status;
    }
#endif

#if ADEV_FLASH25Q_ENABLE
    status = appSystemFlashInit();
    if (status != A_STATUS_OK) {
        return status;
    }
#endif

#if APP_DATABASE_ENABLE
    status = appDatabaseInit();
    if (status != A_STATUS_OK) {
        return status;
    }
#endif

#if ABUS_ENABLE
    /* Shell 就绪后初始化数据服务，供后续业务协议借用。 */
    status = appSigInit();
    if (status != A_STATUS_OK) {
        return status;
    }
    status = appSigTaskInit();
    if (status != A_STATUS_OK) {
        return status;
    }
#endif

    return A_STATUS_OK;
}
