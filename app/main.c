#include "aDrv.h"
#include "aOS.h"
#include "system.h"
#include "app_config.h"

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
    aStatus_t status = aDrvInit();
    if (status != A_STATUS_OK) {
        appFatal(AOS_FAULT_APP_INIT, status, "aDrvInit");
    }
    status = aOSInit();
    if (status != A_STATUS_OK) {
        appFatal(AOS_FAULT_APP_INIT, status, "aOSInit");
    }
    status = aOSCreateTask(appInitTask, "appInit", APP_INIT_TASK_STACK_BYTES,
                          NULL, APP_INIT_TASK_PRIORITY, NULL);
    if (status != A_STATUS_OK) {
        appFatal(AOS_FAULT_APP_INIT, status, "aOSCreateTask(appInit)");
    }

    aOSRun();
}
