#include "aOS.h"
void aOSInit(void) {}
aStatus_t aOSCreateTask(aOSTaskFunction_t function, const char *name,
                        uint16_t stack_words, void *argument,
                        UBaseType_t priority, TaskHandle_t *handle)
{
    if ((function == NULL) || (name == NULL) || (stack_words == 0U) ||
        (priority >= configMAX_PRIORITIES)) return A_STATUS_INVALID_PARAM;
    return xTaskCreate(function, name, stack_words, argument, priority, handle) == pdPASS ?
           A_STATUS_OK : A_STATUS_NO_MEMORY;
}
void aOSRun(void) { vTaskStartScheduler(); for (;;) {} }
void aOSDelayMs(uint32_t milliseconds) { vTaskDelay(pdMS_TO_TICKS(milliseconds)); }
uint32_t aOSGetTickMs(void) { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }
void vApplicationMallocFailedHook(void) { taskDISABLE_INTERRUPTS(); for (;;) {} }
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{ (void)task; (void)task_name; taskDISABLE_INTERRUPTS(); for (;;) {} }
