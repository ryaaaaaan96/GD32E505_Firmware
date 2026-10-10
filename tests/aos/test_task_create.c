/* 验证入口与参数直传、栈容量换算，以及业务任务显式退出。 */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../../platform/aOS/backend/freertos/aOS_freertos.c"

static int task_token;
static void (*entry)(void *);
static void *entry_arg;
static uint16_t stack_depth;
static int depth, fail_create, ran, deleted, run_immediately;
static jmp_buf finished;
void mock_enter(void) { ++depth; }
void mock_exit(void) { assert(depth); --depth; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint16_t words,
                      void *arg, UBaseType_t priority, TaskHandle_t *out)
{
    assert(depth == 0 && name && priority == AOS_TASK_PRIO_NORMAL);
    if (fail_create) {
        /* 模拟内核任务内存不足：钩子记录错误，创建调用仍返回失败。 */
        vApplicationMallocFailedHook();
        return pdFALSE;
    }
    entry = fn;
    entry_arg = arg;
    stack_depth = words;
    *out = &task_token;
    /* 新任务可能在创建函数返回前运行并自行退出。 */
    if (run_immediately) {
        if (setjmp(finished) == 0) fn(arg);
    }
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task)
{
    ++deleted;
    if (task == NULL) longjmp(finished, 1);
}
static void body(void *arg)
{
    assert(arg == &ran && depth == 0);
    ++ran;
    aOSTaskExit();
}
int main(void)
{
    aOSTaskHandle_t task = NULL;
    aOSTaskConfig_t config = AOS_TASK_CONFIG_DEFAULT;
    assert(config.function == NULL && config.argument == NULL);
    assert(config.stack_bytes == 0 && config.priority == AOS_TASK_PRIO_NORMAL);
    assert(strcmp(config.name, "task") == 0);
    aOSTaskConfigStructInit(NULL);
    assert(aOSCreateTask(NULL, &task) == A_STATUS_INVALID_PARAM && task == NULL);
    assert(aOSCreateTask(&config, &task) == A_STATUS_INVALID_PARAM);
    config.function = body;
    config.argument = &ran;
    config.stack_bytes = 513;
    assert(aOSCreateTask(&config, &task) == A_STATUS_OK);
    assert(stack_depth == 129 && task == &task_token);
    assert(entry == body && entry_arg == &ran);
    /* 创建不得保留调用方配置，重置配置后原任务仍使用原入口和参数。 */
    aOSTaskConfigStructInit(&config);
    assert(config.function == NULL && config.argument == NULL);
    assert(config.stack_bytes == 0 && config.priority == AOS_TASK_PRIO_NORMAL);
    assert(strcmp(config.name, "task") == 0);
    if (setjmp(finished) == 0) entry(entry_arg);
    assert(ran == 1 && deleted == 1);
    config.function = body;
    config.argument = &ran;
    assert(aOSCreateTask(&config, &task) == A_STATUS_OK);
    assert(stack_depth == configMINIMAL_STACK_SIZE);
    aOSDeleteTask(task); /* 未运行的任务可以删除，借用参数仍由应用持有。 */
    assert(ran == 1 && deleted == 2 && depth == 0);
    assert(aOSCreateTask(&config, NULL) == A_STATUS_OK);
    if (setjmp(finished) == 0) entry(entry_arg);
    assert(ran == 2 && deleted == 3);
    run_immediately = 1;
    assert(aOSCreateTask(&config, NULL) == A_STATUS_OK);
    assert(ran == 3 && deleted == 4 && depth == 0);
    run_immediately = 0;
    fail_create = 1;
    task = &task_token;
    assert(aOSCreateTask(&config, &task) == A_STATUS_NO_MEMORY);
    assert(task == NULL);
    assert(g_aOSFaultRecord.code == AOS_FAULT_MALLOC_FAILED && depth == 0);
    fail_create = 0;
    config.stack_bytes = SIZE_MAX;
    assert(aOSCreateTask(&config, &task) == A_STATUS_INVALID_PARAM);
    config.stack_bytes = 512;
    config.priority = 0;
    assert(aOSCreateTask(&config, &task) == A_STATUS_INVALID_PARAM);
    config.priority = AOS_TASK_PRIO_REALTIME + 1U;
    assert(aOSCreateTask(&config, &task) == A_STATUS_INVALID_PARAM);
    config.priority = AOS_TASK_PRIO_NORMAL;
    config.name = NULL;
    task = &task_token;
    assert(aOSCreateTask(&config, &task) == A_STATUS_INVALID_PARAM && task == NULL);
    puts("aOS task direct-entry/stack/explicit-exit/delete/failure tests passed");
}
