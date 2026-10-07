#include "app_modbus_task.h"
#include "app_modbus.h"
#include "aOS.h"
#if ASHELL_ENABLE && APP_MODBUS_MASTER_ENABLE
#include "aShell.h"
#endif

static void modbusTask(void *argument)
{
#if ASHELL_ENABLE && APP_MODBUS_MASTER_ENABLE
    aStatus_t previous = A_STATUS_NOT_READY;
#endif
    (void)argument;
    for (;;) {
        aStatus_t status = appModbusProcess();
#if APP_MODBUS_MASTER_ENABLE
#if ASHELL_ENABLE
        /* 只报告通信状态变化，避免断线时每秒刷屏。 */
        if (status != previous) {
            ASHELL_PRINT("Modbus master: %s (status %d)\r\n",
                         status == A_STATUS_OK ? "online" : "read failed",
                         (int)status);
            previous = status;
        }
#else
        (void)status;
#endif
        aOSDelayMs(1000U);
#else
        if (status != A_STATUS_OK) aOSDelayMs(5U);
#endif
    }
}

aStatus_t appModbusTaskInit(void)
{
    aOSTaskConfig_t task;
    aStatus_t status;

    aOSTaskConfigStructInit(&task);
    task.name = "modbus";
    task.function = modbusTask;
    task.stack_bytes = 4096U;
    task.priority = AOS_TASK_PRIO_NORMAL;
    status = aOSCreateTask(&task, NULL);
    if (status != A_STATUS_OK) (void)appModbusDeInit();
    return status;
}
