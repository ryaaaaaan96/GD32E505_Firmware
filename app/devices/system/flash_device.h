#ifndef APP_FLASH_DEVICE_H
#define APP_FLASH_DEVICE_H
#include "aDev_flash25q.h"

/* SPI1 Flash 的应用层单例；句柄由本模块私有持有。 */
aStatus_t appSystemFlashInit(void);
aStatus_t appSystemFlashGetInfo(aDevFlash25qInfo_t *info);
aStatus_t appSystemFlashRead(const aDevFlash25qReadRequest_t *request);
aStatus_t appSystemFlashWrite(const aDevFlash25qWriteRequest_t *request);
aStatus_t appSystemFlashErase(const aDevFlash25qEraseRequest_t *request);
#endif
