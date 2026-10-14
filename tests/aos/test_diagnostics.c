#include <assert.h>
#include <string.h>
#include "../../platform/aOS/backend/freertos/aOS_freertos.c"

static BaseType_t in_isr, scheduler = taskSCHEDULER_RUNNING;
static unsigned suspended, queries;
static int task_object;
BaseType_t xPortIsInsideInterrupt(void) { return in_isr; }
BaseType_t xTaskGetSchedulerState(void) { return scheduler; }
void vTaskSuspendAll(void) { suspended++; }
BaseType_t xTaskResumeAll(void)
{
    assert(suspended == 1U);
    suspended--;
    return pdFALSE;
}
size_t xPortGetFreeHeapSize(void) { assert(suspended); return 12000U; }
size_t xPortGetMinimumEverFreeHeapSize(void)
{ assert(suspended); return 9000U; }
TaskHandle_t xTaskGetCurrentTaskHandle(void)
{ assert(suspended); return &task_object; }
TaskHandle_t xTaskGetHandle(const char *name)
{
    assert(suspended);
    queries++;
    return strcmp(name, "shell") == 0 ? &task_object : NULL;
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{ assert(suspended && task == &task_object); return 37U; }

int main(void)
{
    aOSHeapInfo_t heap = {0};
    size_t bytes = 999U;
    assert(aOSHeapGetInfo(NULL) == A_STATUS_INVALID_PARAM);
    assert(aOSTaskStackGetFree(NULL, NULL) == A_STATUS_INVALID_PARAM);
    scheduler = taskSCHEDULER_NOT_STARTED;
    assert(aOSHeapGetInfo(&heap) == A_STATUS_NOT_READY);
    assert(aOSTaskStackGetFree(NULL, &bytes) == A_STATUS_NOT_READY);
    scheduler = taskSCHEDULER_RUNNING;
    in_isr = 1;
    assert(aOSHeapGetInfo(&heap) == A_STATUS_INVALID_PARAM);
    assert(aOSTaskStackGetFree(NULL, &bytes) == A_STATUS_INVALID_PARAM);
    in_isr = 0;
    assert(aOSHeapGetInfo(&heap) == A_STATUS_OK);
    assert(heap.total == 40960U && heap.free == 12000U &&
           heap.minimum_free == 9000U && suspended == 0U);
    assert(aOSTaskStackGetFree("missing", &bytes) == A_STATUS_NOT_FOUND);
    assert(bytes == 999U && suspended == 0U);
    assert(aOSTaskStackGetFree("shell", &bytes) == A_STATUS_OK);
    assert(bytes == 148U && suspended == 0U);
    assert(aOSTaskStackGetFree(NULL, &bytes) == A_STATUS_OK);
    assert(bytes == 148U);
    assert(aOSTaskStackGetFree("", &bytes) == A_STATUS_INVALID_PARAM);
    assert(aOSTaskStackGetFree("1234567890123456", &bytes) ==
           A_STATUS_INVALID_PARAM);
    assert(queries == 2U && suspended == 0U);
    return 0;
}
