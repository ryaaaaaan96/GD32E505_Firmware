#ifndef ADRV_H
#define ADRV_H

#include "aStatus.h"

#include <stddef.h>
#include <stdint.h>

typedef void (*aDrvInterruptCallback_t)(void *argument);

aStatus_t aDrvInit(void);

#endif
