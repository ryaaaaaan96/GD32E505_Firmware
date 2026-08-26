#ifndef ACORE_RUNTIME_H
#define ACORE_RUNTIME_H

#include "aLib.h"

#include <stddef.h>

aSSize_t aCoreRuntimeRead(void *buffer, size_t size);
aSSize_t aCoreRuntimeWrite(const void *data, size_t size);

#endif
