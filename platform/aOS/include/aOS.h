#ifndef AOS_H
#define AOS_H

#include "aLib.h"
#include "aStatus.h"

#include <stdint.h>

#define AOS_TASK_PRIO_LOWEST 1U
#define AOS_TASK_PRIO_LOW 2U
#define AOS_TASK_PRIO_BELOW_NORMAL 3U
#define AOS_TASK_PRIO_NORMAL 4U
#define AOS_TASK_PRIO_ABOVE_NORMAL 5U
#define AOS_TASK_PRIO_HIGH 6U
#define AOS_TASK_PRIO_REALTIME 7U

typedef void (*aOSTaskFunction_t)(void *argument);
typedef void *aOSTaskHandle_t;

aStatus_t aOSInit(void);
aStatus_t aOSCreateTask(aOSTaskFunction_t function, const char *name,
                        uint16_t stack_words, void *argument,
                        uint32_t priority, aOSTaskHandle_t *handle);
void aOSRun(void) ALIB_NORETURN;
void aOSDelayMs(uint32_t milliseconds);
void aOSYield(void);
uint32_t aOSGetUptimeMs(void);
aErrno_t aOSGetErrno(void);
void aOSSetErrno(aErrno_t error);

#endif
