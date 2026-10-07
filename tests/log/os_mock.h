#ifndef TEST_LOG_OS_MOCK_H
#define TEST_LOG_OS_MOCK_H

#include "aOS.h"

void testFailMutexCreate(aBool_t fail);
void testFailNextLock(aStatus_t status);
unsigned testMutexCount(void);
void testSetUptime(uint32_t value);

#endif
