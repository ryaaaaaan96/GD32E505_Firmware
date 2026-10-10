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
