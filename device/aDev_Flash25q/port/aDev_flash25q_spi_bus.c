#include "aDev_flash25q_internal.h"

/* 全双工交换一个字节；发送命令时也必须读取并丢弃接收字节。 */
static aStatus_t spi_exchange_byte(
    aDevFlash25qHandle_t *handle, uint8_t tx, uint8_t *rx)
{
    aStatus_t status;
    aBool_t complete;

    do {
        if (aTimepointExpired(&handle->deadline, aOSGetUptimeMs()))
            return A_STATUS_TIMEOUT;
        status = aDrvSpiIsComplete(&handle->bus->spi, &complete);
        if (status != A_STATUS_OK) return status;
        status = aDrvSpiTryWrite(&handle->bus->spi, &tx);
    } while (status == A_STATUS_BUSY);
    if (status != A_STATUS_OK) return status;
    do {
        if (aTimepointExpired(&handle->deadline, aOSGetUptimeMs()))
            return A_STATUS_TIMEOUT;
        status = aDrvSpiIsComplete(&handle->bus->spi, &complete);
        if (status != A_STATUS_OK) return status;
        status = aDrvSpiTryRead(&handle->bus->spi, rx);
    } while (status == A_STATUS_BUSY);
    return status;
}

aStatus_t aDevFlash25qSpiTransaction(
    aDevFlash25qHandle_t *handle,
    const uint8_t *tx, size_t tx_size,
    uint8_t *rx, size_t rx_size)
{
    aDevFlash25qBus_t *bus = handle->bus;
    aStatus_t status;
    aStatus_t cleanup;
    aBool_t complete = A_FALSE;
    uint8_t discard;
    size_t i;

    if (bus->fault) return A_STATUS_NOT_READY;
    status = aOSMutexLock(bus->mutex,
        aTimepointRemaining(&handle->deadline, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) return status;
    status = aDrvGpioWrite(&handle->cs, ADRV_GPIO_LOW);
    for (i = 0U; (i < tx_size) && (status == A_STATUS_OK); ++i)
        status = spi_exchange_byte(handle, tx[i], &discard);
    for (i = 0U; (i < rx_size) && (status == A_STATUS_OK); ++i)
        status = spi_exchange_byte(handle, 0xFFU, &rx[i]);
    while ((status == A_STATUS_OK) && !complete) {
        status = aDrvSpiIsComplete(&bus->spi, &complete);
        if (!complete && (status == A_STATUS_OK) &&
            aOSPollWaitExpired(&handle->deadline))
            status = A_STATUS_TIMEOUT;
    }
    if (status != A_STATUS_OK) {
        /* 停止硬件再释放片选；总线故障后禁止继续使用残留事务。 */
        (void)aDrvSpiAbort(&bus->spi);
        bus->fault = A_TRUE;
    }
    cleanup = aDrvGpioWrite(&handle->cs, ADRV_GPIO_HIGH);
    if (cleanup != A_STATUS_OK) {
        bus->fault = A_TRUE;
        if (status == A_STATUS_OK) status = cleanup;
    }
    cleanup = aOSMutexUnlock(bus->mutex);
    return status == A_STATUS_OK ? cleanup : status;
}
