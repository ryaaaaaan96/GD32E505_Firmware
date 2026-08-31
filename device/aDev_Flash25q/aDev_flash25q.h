#ifndef ADEV_FLASH25Q_H
#define ADEV_FLASH25Q_H

#include "aDrv_qspi.h"
#include "aLib.h"

#define ADEV_FLASH_IOCTL_QSPI_FAST_READ 0x01U
#define ADEV_FLASH25Q_DEVICE_COUNT 1U

typedef struct {
    aDrvQspiConfig_t drv_config;
    uint8_t flashIndex;
    uint32_t capacity;
} aDevFlash25qConfig_t;

typedef struct {
    aDrvQspiHandle_t qspi;
    uint32_t size;
    uint8_t flash_index;
    aBool_t initialized;
    aBool_t fast_read;
} aDevFlash25qHandle_t;

void aDevFlash25qConfigStructInit(aDevFlash25qConfig_t *config);
void aDevFlash25qHandleStructInit(aDevFlash25qHandle_t *handle);
aStatus_t aDevFlash25qInit(const aDevFlash25qConfig_t *config,
                              aDevFlash25qHandle_t *handle);
aStatus_t aDevFlash25qDeInit(aDevFlash25qHandle_t *handle);
aStatus_t aDevFlash25qRead(aDevFlash25qHandle_t *handle, uint32_t address,
                              uint8_t *data, uint32_t size);
aStatus_t aDevFlash25qWrite(aDevFlash25qHandle_t *handle, uint32_t address,
                            const uint8_t *data, uint32_t size,
                            aTimeout_t timeout);
aStatus_t aDevFlash25qErase(aDevFlash25qHandle_t *handle, uint32_t address,
                            uint32_t size, aTimeout_t timeout);
aStatus_t aDevFlash25qChipErase(aDevFlash25qHandle_t *handle,
                                aTimeout_t timeout);
uint32_t aDevFlash25qGetSize(const aDevFlash25qHandle_t *handle);
aStatus_t aDevFlash25qHandleIsValid(const aDevFlash25qHandle_t *handle);
aStatus_t aDevFlash25qIoCtl(aDevFlash25qHandle_t *handle, uint32_t command,
                               void *argument);
aDevFlash25qHandle_t *aDevFlash25qGetDevice(uint8_t flash_index);

#endif
