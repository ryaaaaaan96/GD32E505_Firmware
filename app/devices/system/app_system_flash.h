#ifndef APP_SYSTEM_FLASH_H
#define APP_SYSTEM_FLASH_H
#include "aDev_flash25q.h"

/* SPI1 Flash application singleton; its handle remains private. */
aStatus_t appSystemFlashInit(void);
aStatus_t appSystemFlashGetInfo(aDevFlash25qInfo_t *info);
aStatus_t appSystemFlashRead(const aDevFlash25qReadRequest_t *request);
aStatus_t appSystemFlashWrite(const aDevFlash25qWriteRequest_t *request);
aStatus_t appSystemFlashErase(const aDevFlash25qEraseRequest_t *request);
#endif
