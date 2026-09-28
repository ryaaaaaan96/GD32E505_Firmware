/* 验证自退出使用后端 NULL 语义，而删除指定任务的 NULL 参数仍为空操作。 */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "../../platform/aOS/backend/freertos/aOS_freertos.c"

static jmp_buf exited;
static unsigned deletes;
static TaskHandle_t deleted;

void vTaskDelete(TaskHandle_t handle)
{
    ++deletes;
    deleted = handle;
    if (handle == NULL) longjmp(exited, 1);
}

void mock_enter(void) {}
void mock_exit(void) {}
void *pvTaskGetThreadLocalStoragePointer(TaskHandle_t task, BaseType_t slot)
{ (void)task; (void)slot; return NULL; }
void vTaskSetThreadLocalStoragePointer(TaskHandle_t task, BaseType_t slot, void *p)
{ (void)task; (void)slot; assert(p == NULL); }
void vPortFree(void *p) { assert(p == NULL); }
int main(void)
{
    aOSDeleteTask(NULL);
    assert(deletes == 0U);
    aOSDeleteTask(&deletes);
    assert(deletes == 1U && deleted == &deletes);
    if (setjmp(exited) == 0) aOSTaskExit();
    assert(deletes == 2U && deleted == NULL);
    puts("aOS task exit/delete semantics passed");
    return 0;
}
