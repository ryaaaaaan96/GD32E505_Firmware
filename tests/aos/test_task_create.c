/* Public stack units, bootstrap ownership, and task return semantics. */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../platform/aOS/backend/freertos/aOS_freertos.c"

static void *tls[2];
static void (*entry)(void *);
static void *entry_arg;
static size_t allocated;
static uint16_t stack_depth;
static int depth, fail_alloc, fail_create, ran, deleted;
static jmp_buf finished;
void mock_enter(void) { ++depth; }
void mock_exit(void) { assert(depth); --depth; }
void *pvPortMalloc(size_t n)
{
    if (fail_alloc) { vApplicationMallocFailedHook(); return NULL; }
    void *p = malloc(n); assert(p); ++allocated; return p;
}
void vPortFree(void *p) { if (p) { --allocated; free(p); } }
void vTaskSetThreadLocalStoragePointer(TaskHandle_t task, BaseType_t slot, void *p)
{ (void)task; assert(depth > 0); tls[slot] = p; }
void *pvTaskGetThreadLocalStoragePointer(TaskHandle_t task, BaseType_t slot)
{ (void)task; return tls[slot]; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint16_t words,
                      void *arg, UBaseType_t priority, TaskHandle_t *out)
{
    assert(depth && name && priority == AOS_TASK_PRIO_NORMAL);
    if (fail_create) return pdFALSE;
    entry = fn; entry_arg = arg; stack_depth = words; *out = tls;
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task)
{
    ++deleted;
    if (task == NULL) longjmp(finished, 1);
}
static void body(void *arg)
{ assert(arg == &ran && allocated == 0 && tls[1] == NULL && depth == 0); ++ran; }
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
    assert(stack_depth == 129 && allocated == 1 && task == tls);
    /* Creation must not retain the caller's configuration storage. */
    aOSTaskConfigStructInit(&config);
    assert(config.function == NULL && config.argument == NULL);
    assert(config.stack_bytes == 0 && config.priority == AOS_TASK_PRIO_NORMAL);
    assert(strcmp(config.name, "task") == 0);
    if (setjmp(finished) == 0) entry(entry_arg);
    assert(ran == 1 && deleted == 1 && allocated == 0);
    config.function = body;
    config.argument = &ran;
    assert(aOSCreateTask(&config, &task) == A_STATUS_OK);
    assert(stack_depth == configMINIMAL_STACK_SIZE);
    aOSDeleteTask(task); /* Never started: bootstrap must still be freed. */
    assert(allocated == 0 && tls[1] == NULL && deleted == 2 && depth == 0);
    assert(aOSCreateTask(&config, NULL) == A_STATUS_OK);
    if (setjmp(finished) == 0) entry(entry_arg);
    assert(ran == 2 && allocated == 0);
    fail_create = 1;
    task = tls;
    assert(aOSCreateTask(&config, &task) == A_STATUS_NO_MEMORY);
    assert(allocated == 0 && task == NULL);
    fail_create = 0; fail_alloc = 1;
    assert(aOSCreateTask(&config, &task) == A_STATUS_NO_MEMORY);
    assert(g_aOSFaultRecord.code == AOS_FAULT_MALLOC_FAILED && depth == 0);
    config.stack_bytes = SIZE_MAX;
    assert(aOSCreateTask(&config, &task) == A_STATUS_INVALID_PARAM);
    config.stack_bytes = 512;
    config.priority = 0;
    assert(aOSCreateTask(&config, &task) == A_STATUS_INVALID_PARAM);
    config.priority = AOS_TASK_PRIO_REALTIME + 1U;
    assert(aOSCreateTask(&config, &task) == A_STATUS_INVALID_PARAM);
    config.priority = AOS_TASK_PRIO_NORMAL;
    config.name = NULL;
    task = tls;
    assert(aOSCreateTask(&config, &task) == A_STATUS_INVALID_PARAM && task == NULL);
    assert(allocated == 0);
    puts("aOS task config/default/stack/return/early-delete/allocation failure passed");
}
