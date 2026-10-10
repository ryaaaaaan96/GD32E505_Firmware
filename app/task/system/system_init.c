#include "system_init.h"
#include "app_config.h"
#include "aOS.h"
#include "system_device.h"
#include "protocol.h"
#if ADEV_FLASH25Q_ENABLE
#include "flash_device.h"
#endif
#if AMEMORY_ENABLE && ADEV_FLASH25Q_ENABLE
#include "memory_config.h"
#endif
#if APP_DATABASE_ENABLE
#include "database_service.h"
#endif
#if APP_LOG_ENABLE
#include "log_service.h"
#endif
#if ASHELL_ENABLE
#include "aDrv_basic.h"
#include "aShell.h"
#include <stdatomic.h>
#endif

/* --------------------------------------------------------------------------
 * 系统状态指示灯：设备初始化与周期闪烁任务。
 * -------------------------------------------------------------------------- */
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
    task_config.stack_bytes = 256U;
    task_config.priority = AOS_TASK_PRIO_NORMAL;
    task_config.argument = led;
    status = aOSCreateTask(&task_config, NULL);
    if (status != A_STATUS_OK) {
        return status;
    }

    return A_STATUS_OK;
}

/* --------------------------------------------------------------------------
 * 系统控制台：Shell 初始化、输入处理与输出队列发送任务。
 * -------------------------------------------------------------------------- */
#if ASHELL_ENABLE

/* 初始化期间允许日志入队，服务全部就绪后才处理命令。 */
static atomic_bool shell_services_ready = ATOMIC_VAR_INIT(0);

static void shellTask(void *argument)
{
    (void)argument;
    while (!atomic_load_explicit(&shell_services_ready,
                                 memory_order_acquire)) {
        aOSDelayMs(1U);
    }
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
    aStatus_t cleanup_status;
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
        cleanup_status = appSystemConsoleDeInit();
        if (cleanup_status != A_STATUS_OK) return cleanup_status;
    }
    return status;
}

#endif

/* --------------------------------------------------------------------------
 * 系统初始化编排：各功能内部完成具体配置，服务就绪后允许 Shell 任务处理命令。
 * -------------------------------------------------------------------------- */
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

#if APP_LOG_ENABLE
    /* 当前日志输出依赖 Shell 队列，由系统在消费者启动前单独初始化。 */
    status = appLogInit();
    if (status != A_STATUS_OK) {
#if ASHELL_ENABLE
        /* Shell 任务仍在就绪门控之前，日志初始化失败未产生后台使用者。 */
        const aStatus_t cleanup_status = appSystemConsoleDeInit();
        if (cleanup_status != A_STATUS_OK) return cleanup_status;
#endif
        return status;
    }
#endif

#if ADEV_FLASH25Q_ENABLE
    status = appSystemFlashInit();
    if (status != A_STATUS_OK) {
        return status;
    }
#endif

#if AMEMORY_ENABLE && ADEV_FLASH25Q_ENABLE
    status = appSystemMemoryInit();
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

    /* 点表、协议端口与通信任务统一由 protocol 内部编排。 */
    status = protocolInit();
    if (status != A_STATUS_OK) {
        return status;
    }

#if ASHELL_ENABLE
    /* 发布服务初始化结果，允许已创建的 Shell 任务开始处理命令。 */
    atomic_store_explicit(&shell_services_ready, 1, memory_order_release);
#endif

    return A_STATUS_OK;
}
