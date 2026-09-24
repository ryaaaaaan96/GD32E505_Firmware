#ifndef AOS_H
#define AOS_H

#include "aLib.h"
#include "aStatus.h"

#include <stddef.h>
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

/* Opaque task mutex. A mutex is never acquired or released from an ISR. */
typedef void *aOSMutex_t;
typedef void *aOSRecursiveMutex_t;

typedef enum {
    AOS_FAULT_NONE = 0U,
    AOS_FAULT_APP_INIT = 1U,
    AOS_FAULT_SCHEDULER_RETURNED = 2U,
    AOS_FAULT_MALLOC_FAILED = 3U,
    AOS_FAULT_STACK_OVERFLOW = 4U,
} aOSFaultCode_t;

typedef struct {
    volatile uint32_t code;
    volatile int32_t status;
    const char *volatile context;
} aOSFaultRecord_t;

extern aOSFaultRecord_t g_aOSFaultRecord;

aStatus_t aOSInit(void);
/* Validate an IRQ priority before enabling an ISR that calls aOS. */
aStatus_t aOSValidateIsrPriority(uint32_t priority);
aStatus_t aOSCreateTask(aOSTaskFunction_t function, const char *name,
                        uint16_t stack_words, void *argument,
                        uint32_t priority, aOSTaskHandle_t *handle);
void aOSDeleteTask(aOSTaskHandle_t handle);
void *aOSAlloc(size_t size);
void aOSFree(void *memory);
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

/* Task-context mutex operations with the same timeout model as wait objects. */
aStatus_t aOSMutexCreate(aOSMutex_t *mutex);
void aOSMutexDestroy(aOSMutex_t *mutex);
aStatus_t aOSMutexLock(aOSMutex_t mutex, aTimeout_t timeout);
aStatus_t aOSMutexUnlock(aOSMutex_t mutex);
aStatus_t aOSRecursiveMutexCreate(aOSRecursiveMutex_t *mutex);
void aOSRecursiveMutexDestroy(aOSRecursiveMutex_t *mutex);
aStatus_t aOSRecursiveMutexLock(aOSRecursiveMutex_t mutex,
                               aTimeout_t timeout);
aStatus_t aOSRecursiveMutexUnlock(aOSRecursiveMutex_t mutex);
void aOSRecordFault(aOSFaultCode_t code, aStatus_t status,
                    const char *context);

aErrno_t aOSGetErrno(void);
void aOSSetErrno(aErrno_t error);
aSSize_t aOSFailWithStatus(aStatus_t status);
aSSize_t aOSFailWithTimeout(aTimeout_t timeout);
aBool_t aOSPollWaitExpired(const aTimepoint_t *timepoint);

#endif
