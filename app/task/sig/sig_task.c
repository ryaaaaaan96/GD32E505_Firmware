#include "sig_task.h"
#include "IDU_sig_table.h"
#include "app_config.h"
#include "aBus.h"
#include "aOS.h"

/* 变量属于任务，通过链接段注册绑定，aBus 不为它另分配数据区。 */
static uint32_t counter;
ABUS_RAM_BIND_EXPORT(counter_binding,
                     PROTOCOL_BUS_INSTANCE_ID,
                     IDU_SIG_DEVICE_ID,
                     IDU_SIG_COUNTER,
                     counter);

static void sigTask(void *argument)
{
    (void)argument;
    for (;;) {
        aOSDelayMs(1000U);
        /* 绑定测试不做并发同步；UINT32_MAX 后回绕到零。 */
        counter++;
    }
}

aStatus_t appSigTaskInit(void)
{
    aOSTaskConfig_t config;

    aOSTaskConfigStructInit(&config);
    config.name = "sig_test";
    config.function = sigTask;
    config.stack_bytes = 1024U;
    config.priority = AOS_TASK_PRIO_NORMAL;
    return aOSCreateTask(&config, NULL);
}
