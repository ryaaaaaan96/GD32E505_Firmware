/* 模拟调度器启动边界，验证实际 main 函数的初始化顺序。 */
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#define main firmware_main
#include "../../app/main.c"
#undef main

static jmp_buf done;
static unsigned stage, scenario;
static aOSTaskFunction_t entry;
static const char *fault;

aStatus_t aDrvInit(void)
{
    assert(stage++ == 0U);
    return A_STATUS_OK;
}
aStatus_t aOSInit(void)
{
    assert(stage++ == 1U);
    return A_STATUS_OK;
}
aStatus_t aOSCreateTask(aOSTaskFunction_t fn, const char *name,
                        size_t stack, void *arg, uint32_t priority,
                        aOSTaskHandle_t *handle)
{
    assert(stage++ == 2U && strcmp(name, "appInit") == 0);
    assert(stack == 4096U && priority == AOS_TASK_PRIO_HIGH && arg == NULL);
    assert(handle == NULL); /* 初始化任务不需要向应用暴露句柄。 */
    if (scenario == 1U) return A_STATUS_NO_MEMORY;
    entry = fn;
    return A_STATUS_OK;
}
void aOSRun(void)
{
    assert(stage++ == 3U);
    entry(NULL);
    assert(0 && "task entry must not return");
    for (;;) {}
}
aStatus_t aSystemInit(void)
{
    assert(stage++ == 4U); /* 必须在调度器启动后执行，不能由 main 直接调用。 */
    return scenario == 2U ? A_STATUS_ERROR : A_STATUS_OK;
}
void aOSTaskExit(void)
{
    assert(stage++ == 5U);
    longjmp(done, 1);
}
void aOSRecordFault(aOSFaultCode_t code, aStatus_t status, const char *context)
{
    assert(code == AOS_FAULT_APP_INIT && status != A_STATUS_OK);
    fault = context;
    longjmp(done, 1);
}
int main(void)
{
    for (scenario = 0U; scenario < 3U; ++scenario) {
        stage = 0U;
        fault = NULL;
        if (setjmp(done) == 0) (void)firmware_main();
        if (scenario == 0U) assert(stage == 6U && fault == NULL);
        if (scenario == 1U)
            assert(stage == 3U && strcmp(fault, "aOSCreateTask(appInit)") == 0);
        if (scenario == 2U)
            assert(stage == 5U && strcmp(fault, "aSystemInit") == 0);
    }
    return 0;
}
