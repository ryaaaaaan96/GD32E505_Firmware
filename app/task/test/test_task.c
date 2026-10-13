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

#if APP_MODBUS_ENABLE && APP_MODBUS_MASTER_ENABLE && ABUS_EVENT_ENABLE
#include "protocol.h"
#include "FAN_sig_table.h"
#if ASHELL_ENABLE
#include "aShell.h"
#endif

/* 请求和新值保持稳定直到全部接收者完成；本 Demo 的目标只登记一个节点。 */
static aBusRequest_t write_request;
static aBusDelivery_t write_delivery[1];
static uint32_t write_value = 100U;
static aStatus_t write_status;
static aBool_t write_report;

static void writeCompleted(void *context, const aBusNotifyInput_t *input,
    aStatus_t status)
{
    (void)context;
    (void)input;
    /* 完成回调只记结果；由测试任务在 Done 后打印。 */
    write_status = status;
}

static void modbusWriteTest(void)
{
    if (!aBusRequestDone(&write_request)) return;
    if (write_report) {
#if ASHELL_ENABLE
        ASHELL_PRINT("FAN TEST_003 write %lu: status %d\r\n",
                     (unsigned long)write_value, (int)write_status);
#endif
        write_report = A_FALSE;
    }
    write_value = write_value >= 200U ? 101U : write_value + 1U;
    const aBusRequestConfig_t request = {
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_003, ABUS_SIG_WHOLE},
        .event = ABUS_REQUEST_WRITE,
        .data = &write_value, .size = sizeof(write_value),
        .complete = writeCompleted
    };
    aBusDispatchResult_t result;
    aStatus_t status = protocolRequest(&request, &write_request, &result);
    if (result.accepted != 0U) write_report = A_TRUE;
    else {
#if ASHELL_ENABLE
        ASHELL_PRINT("FAN test not accepted: status %d\r\n", (int)status);
#else
        (void)status;
#endif
    }
}
#endif

/* 当前每秒执行 TEST_001 测试，后续周期测试在本模块统一安排。 */
static void testTask(void *argument)
{
    (void)argument;
#if APP_MODBUS_ENABLE && APP_MODBUS_MASTER_ENABLE && ABUS_EVENT_ENABLE
    unsigned seconds = 0U;
    aBusRequestStructInit(&write_request, write_delivery, 1U);
#endif
    for (;;) {
        aOSDelayMs(1000U);
        /* 绑定测试不做并发同步；UINT32_MAX 后回绕到零。 */
        test_value++;
#if APP_MODBUS_ENABLE && APP_MODBUS_MASTER_ENABLE && ABUS_EVENT_ENABLE
        if (++seconds == 5U) {
            seconds = 0U;
            modbusWriteTest();
        }
#endif
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
