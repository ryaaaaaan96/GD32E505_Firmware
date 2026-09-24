#ifndef ADATABASE_H
#define ADATABASE_H

#include "aDev_flash25q.h"
#include "aLib.h"

/** Bind the application-owned Flash25Q before initializing FlashDB instances. */
aStatus_t aDataBaseBindFlash25q(aDevFlash25qHandle_t *handle);
aStatus_t aDataBaseUnbindFlash25q(aDevFlash25qHandle_t *handle);

#endif
