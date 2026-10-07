#ifndef ADEV_FLASH25Q_INTERNAL_H
#define ADEV_FLASH25Q_INTERNAL_H
#include "aDev_flash25q_instance.h"
#include "aOS.h"

/* 内部 SPI 事务：加锁、拉低片选、先发送再接收，完成后释放片选。
 * 复用实例的操作总超时；不组织 Flash 命令，也不轮询芯片忙状态。 */
aStatus_t aDevFlash25qSpiTransaction(
    aDevFlash25qHandle_t *handle,
    const uint8_t *tx, size_t tx_size,
    uint8_t *rx, size_t rx_size);
#endif
