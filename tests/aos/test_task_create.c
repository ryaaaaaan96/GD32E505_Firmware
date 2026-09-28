/* Public stack units, bootstrap ownership, and task return semantics. */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
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
    assert(aOSCreateTask(body, "test", 513, &ran, 4, &task) == A_STATUS_OK);
    assert(stack_depth == 129 && allocated == 1 && task == tls);
    if (setjmp(finished) == 0) entry(entry_arg);
    assert(ran == 1 && deleted == 1 && allocated == 0);
    assert(aOSCreateTask(body, "test", 0, &ran, 4, &task) == A_STATUS_OK);
    assert(stack_depth == configMINIMAL_STACK_SIZE);
    aOSDeleteTask(task); /* Never started: bootstrap must still be freed. */
    assert(allocated == 0 && tls[1] == NULL && deleted == 2 && depth == 0);
    fail_create = 1;
    assert(aOSCreateTask(body, "test", 512, NULL, 4, &task) == A_STATUS_NO_MEMORY);
    assert(allocated == 0 && task == NULL);
    fail_create = 0; fail_alloc = 1;
    assert(aOSCreateTask(body, "test", 512, NULL, 4, &task) == A_STATUS_NO_MEMORY);
    assert(g_aOSFaultRecord.code == AOS_FAULT_MALLOC_FAILED && depth == 0);
    assert(aOSCreateTask(body, "test", SIZE_MAX, NULL, 4, &task) == A_STATUS_INVALID_PARAM);
    assert(aOSCreateTask(body, "test", 512, NULL, 0, &task) == A_STATUS_INVALID_PARAM);
    puts("aOS stack bytes/default/return/early-delete/allocation failure passed");
}
