#ifndef AOS_H
#define AOS_H
#include "aStatus.h"
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#define AOS_TASK_PRIO_LOWEST 1U
#define AOS_TASK_PRIO_LOW 2U
#define AOS_TASK_PRIO_BELOW_NORMAL 3U
#define AOS_TASK_PRIO_NORMAL 4U
#define AOS_TASK_PRIO_ABOVE_NORMAL 5U
#define AOS_TASK_PRIO_HIGH 6U
#define AOS_TASK_PRIO_REALTIME 7U
typedef void (*aOSTaskFunction_t)(void *argument);
void aOSInit(void);
aStatus_t aOSCreateTask(aOSTaskFunction_t function, const char *name,
                        uint16_t stack_words, void *argument,
                        UBaseType_t priority, TaskHandle_t *handle);
void aOSRun(void) __attribute__((noreturn));
void aOSDelayMs(uint32_t milliseconds);
uint32_t aOSGetTickMs(void);
static inline void aOS_Init(void) { aOSInit(); }
static inline void aOS_Run(void) { aOSRun(); }
#endif
