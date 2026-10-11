#include "test_task.h"

#if ABUS_ENABLE
#include "IDU_sig_table.h"
#include "app_config.h"
#include "aBus.h"
#include "aOS.h"

/* --------------------------------------------------------------------------
 * TEST_001 静态绑定测试：变量由测试任务持有，aBus 不另分配数据区。
 * -------------------------------------------------------------------------- */
static uint32_t test_value;
ABUS_RAM_BIND_EXPORT(test_binding,
                     PROTOCOL_BUS_INSTANCE_ID,
                     IDU_SIG_DEVICE_ID,
                     IDU_SIG_TEST_001,
                     test_value);

/* 当前每秒执行 TEST_001 测试，后续周期测试在本模块统一安排。 */
static void testTask(void *argument)
{
    (void)argument;
    for (;;) {
        aOSDelayMs(1000U);
        /* 绑定测试不做并发同步；UINT32_MAX 后回绕到零。 */
        test_value++;
    }
}
#endif

/* --------------------------------------------------------------------------
 * 应用测试任务入口：由 main 调用，测试项的依赖和任务创建在本模块管理。
 * -------------------------------------------------------------------------- */
aStatus_t appTestTaskInit(void)
{
#if ABUS_ENABLE
    aOSTaskConfig_t config;

    aOSTaskConfigStructInit(&config);
    config.name = "test";
    config.function = testTask;
    config.stack_bytes = 1024U;
    config.priority = AOS_TASK_PRIO_NORMAL;
    return aOSCreateTask(&config, NULL);
#else
    /* 当前没有可运行的测试项，不创建空任务。 */
    return A_STATUS_OK;
#endif
}
