#ifndef TEST_AOS_H
#define TEST_AOS_H
#include "aLib.h"
uint32_t aOSGetUptimeMs(void);
aStatus_t aOSValidateIsrPriority(uint32_t priority);
void aOSYield(void);
aStatus_t aOSWaitObjectCreate(void **object);
void aOSWaitObjectDestroy(void **object);
aStatus_t aOSWaitObjectWait(void *object, aTimeout_t timeout);
void aOSWaitObjectNotifyFromISR(void *object);
aStatus_t aOSMutexCreate(void **mutex);
void aOSMutexDestroy(void **mutex);
aStatus_t aOSMutexLock(void *mutex, aTimeout_t timeout);
aStatus_t aOSMutexUnlock(void *mutex);
aSSize_t aOSFailWithStatus(aStatus_t status);
aSSize_t aOSFailWithTimeout(aTimeout_t timeout);
aBool_t aOSPollWaitExpired(const aTimepoint_t *end);
#endif
