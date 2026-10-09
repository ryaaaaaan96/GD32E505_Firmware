#include "aDrv.h"
#include "aOS.h"
#include "system_init.h"

static void appFatal(aOSFaultCode_t code, aStatus_t status,
                     const char *context)
{
    aOSRecordFault(code, status, context);
    for (;;) {
    }
}

static void appInitTask(void *argument)
{
    (void)argument;
    const aStatus_t status = aSystemInit();
    if (status != A_STATUS_OK) {
        appFatal(AOS_FAULT_APP_INIT, status, "aSystemInit");
    }

    /* Shell/status 仍在各自模块内完成任务初始化。
     * 本任务仅执行一次，不复用为工作队列；完成后自删除，由 OS 回收栈和任务控制块。 */
    aOSTaskExit();
}

int main(void)
{
    aOSTaskConfig_t task_config;
    aStatus_t status = aDrvInit();
    if (status != A_STATUS_OK) {
        appFatal(AOS_FAULT_APP_INIT, status, "aDrvInit");
    }
    status = aOSInit();
    if (status != A_STATUS_OK) {
        appFatal(AOS_FAULT_APP_INIT, status, "aOSInit");
    }
    aOSTaskConfigStructInit(&task_config);
    task_config.name = "appInit";
    task_config.function = appInitTask;
    task_config.stack_bytes = 4096U;
    task_config.priority = AOS_TASK_PRIO_HIGH;
    status = aOSCreateTask(&task_config, NULL);
    if (status != A_STATUS_OK) {
        appFatal(AOS_FAULT_APP_INIT, status, "aOSCreateTask(appInit)");
    }

    aOSRun();
}
