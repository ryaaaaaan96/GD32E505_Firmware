#ifndef ADEV_FLASH25Q_INTERNAL_H
#define ADEV_FLASH25Q_INTERNAL_H
#include "aDev_flash25q_instance.h"
#include "aOS.h"

aStatus_t aDevFlash25qSpiTransfer(
    aDevFlash25qHandle_t *handle,
    const uint8_t *tx, size_t tx_size,
    uint8_t *rx, size_t rx_size);
#endif
