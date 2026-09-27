#ifndef ADATABASE_FLASH25Q_H
#define ADATABASE_FLASH25Q_H
#include "aDataBase.h"
#include "aDev_flash25q.h"

/* Optional adapter; the application owns the initialized Flash25Q handle.
 * Its lifetime extends through aDataBaseUnbindStorage(). */
aStatus_t aDataBaseBindFlash25q(aDevFlash25qHandle_t *handle);
#endif
