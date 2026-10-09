#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
#define configMINIMAL_STACK_SIZE 128
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *TimerHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 3
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 8
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define taskSCHEDULER_NOT_STARTED 0
#define taskSCHEDULER_RUNNING 1
void mock_enter(void);
void mock_exit(void);
#define taskENTER_CRITICAL() mock_enter()
#define taskEXIT_CRITICAL() mock_exit()
#define taskENTER_CRITICAL_FROM_ISR() (mock_enter(), 0U)
#define taskEXIT_CRITICAL_FROM_ISR(mask) ((void)(mask), mock_exit())
#define portYIELD_FROM_ISR(woken) ((void)(woken))
#define taskYIELD() ((void)0)
#define taskDISABLE_INTERRUPTS() ((void)0)
BaseType_t xTaskGetSchedulerState(void);
BaseType_t xPortIsInsideInterrupt(void);
TaskHandle_t xTimerGetTimerDaemonTaskHandle(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xTimerPendFunctionCall(void (*)(void *, uint32_t), void *, uint32_t, TickType_t);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskCreate(void (*function)(void *), const char *, uint16_t, void *, UBaseType_t, TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
void vTaskStartScheduler(void);
void vTaskDelay(TickType_t);
TickType_t xTaskGetTickCount(void);
uint32_t ulTaskNotifyTakeIndexed(UBaseType_t, BaseType_t, TickType_t);
BaseType_t xTaskNotifyGiveIndexed(TaskHandle_t, UBaseType_t);
void vTaskNotifyGiveIndexedFromISR(TaskHandle_t, UBaseType_t, BaseType_t *);
void *pvTaskGetThreadLocalStoragePointer(TaskHandle_t, BaseType_t);
void vTaskSetThreadLocalStoragePointer(TaskHandle_t, BaseType_t, void *);
void *pvPortMalloc(size_t);
void vPortFree(void *);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
void vSemaphoreDelete(SemaphoreHandle_t);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t);
TimerHandle_t xTimerCreate(const char *, TickType_t, BaseType_t, void *, void (*)(TimerHandle_t));
void *pvTimerGetTimerID(TimerHandle_t);
BaseType_t xTimerChangePeriod(TimerHandle_t, TickType_t, TickType_t);
BaseType_t xTimerStop(TimerHandle_t, TickType_t);
BaseType_t xTimerDelete(TimerHandle_t, TickType_t);
#endif

TickType_t xTaskGetTickCountFromISR(void);
