#ifndef ADRV_H
#define ADRV_H

#include "aLib.h"

#include <stddef.h>
#include <stdint.h>

typedef void (*aDrvInterruptCallback_t)(void *argument);

aStatus_t aDrvInit(void);

#endif
