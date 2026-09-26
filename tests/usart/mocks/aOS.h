#ifndef TEST_AOS_H
#define TEST_AOS_H
#include "aLib.h"
typedef void *aOSMutex_t;
typedef void *aOSWaitObject_t;
typedef struct aOSWorkItem aOSWorkItem_t;
typedef void (*aOSWorkFunction_t)(void *argument);
typedef void *aOSTimer_t;
typedef void (*aOSTimerCallback_t)(void *argument);
typedef uintptr_t aOSCriticalState_t;
struct aOSWorkItem {
    aOSWorkItem_t *next;
    aOSWorkFunction_t function;
    void *argument;
    aBool_t queued;
    aBool_t running;
};
uint32_t aOSGetUptimeMs(void);
void *aOSAlloc(size_t size);
void aOSFree(void *memory);
aStatus_t aOSValidateIsrPriority(uint32_t priority);
void aOSYield(void);
aStatus_t aOSWaitObjectCreate(void **object);
void aOSWaitObjectDestroy(void **object);
aStatus_t aOSWaitObjectWait(void *object, aTimeout_t timeout);
void aOSWaitObjectNotify(void *object);
void aOSWaitObjectNotifyFromISR(void *object);
aStatus_t aOSMutexCreate(void **mutex);
void aOSMutexDestroy(void **mutex);
aStatus_t aOSMutexLock(void *mutex, aTimeout_t timeout);
aStatus_t aOSMutexUnlock(void *mutex);
aStatus_t aOSWorkSubmitFromISR(aOSWorkItem_t *item,
                               aOSWorkFunction_t function, void *argument);
void aOSWorkItemInit(aOSWorkItem_t *item);
aStatus_t aOSWorkWaitIdle(aOSWorkItem_t *item, aTimeout_t timeout);
aStatus_t aOSWorkSubmit(aOSWorkItem_t *item,
                        aOSWorkFunction_t function, void *argument);
aStatus_t aOSTimerCreate(aOSTimer_t *timer, aOSTimerCallback_t callback,
                         void *argument);
aStatus_t aOSTimerStart(aOSTimer_t timer, uint32_t milliseconds);
void aOSTimerStop(aOSTimer_t timer);
void aOSTimerDestroy(aOSTimer_t *timer);
void aOSCriticalEnter(void);
void aOSCriticalExit(void);
aOSCriticalState_t aOSCriticalEnterFromISR(void);
void aOSCriticalExitFromISR(aOSCriticalState_t state);
aSSize_t aOSFailWithStatus(aStatus_t status);
aSSize_t aOSFailWithTimeout(aTimeout_t timeout);
aBool_t aOSPollWaitExpired(const aTimepoint_t *end);
#endif
