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

/*
 * Opaque, coalescing notification object. Multiple notifications before a
 * waiter runs may merge into one wakeup; callers must always recheck their
 * protected condition after Wait() returns. Only one task may wait on an
 * object at a time; a concurrent second waiter receives A_STATUS_BUSY.
 */
typedef void *aOSWaitObject_t;

aStatus_t aOSInit(void);
aStatus_t aOSCreateTask(aOSTaskFunction_t function, const char *name,
                        uint16_t stack_words, void *argument,
                        uint32_t priority, aOSTaskHandle_t *handle);
void aOSRun(void) ALIB_NORETURN;
void aOSDelayMs(uint32_t milliseconds);
void aOSYield(void);
uint32_t aOSGetUptimeMs(void);

/* Task-context lifecycle and wait operations. */
aStatus_t aOSWaitObjectCreate(aOSWaitObject_t *object);
void aOSWaitObjectDestroy(aOSWaitObject_t *object);
aStatus_t aOSWaitObjectWait(aOSWaitObject_t object, aTimeout_t timeout);
void aOSWaitObjectNotify(aOSWaitObject_t object);

/* ISR-only notification; the aOS port performs any required reschedule. */
void aOSWaitObjectNotifyFromISR(aOSWaitObject_t object);
aErrno_t aOSGetErrno(void);
void aOSSetErrno(aErrno_t error);
aSSize_t aOSFailWithStatus(aStatus_t status);
aSSize_t aOSFailWithTimeout(aTimeout_t timeout);
aBool_t aOSPollWaitExpired(const aTimepoint_t *timepoint);

#endif
