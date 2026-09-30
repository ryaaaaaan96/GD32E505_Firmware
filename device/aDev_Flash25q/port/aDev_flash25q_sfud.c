#include "aDev_flash25q_internal.h"

static sfud_err write_read(const sfud_spi *spi,
                           const uint8_t *tx, size_t tx_size,
                           uint8_t *rx, size_t rx_size)
{
    aDevFlash25qHandle_t *handle = spi->user_data;
    aStatus_t status;
    aBool_t status_read;

    if (handle->port_error != A_STATUS_OK) return SFUD_ERR_TIMEOUT;
    status_read = (tx_size == 1U) && (tx[0] == SFUD_CMD_READ_STATUS_REGISTER)
                  && (rx_size == 1U);
    for (;;) {
        if (aTimepointExpired(&handle->deadline, aOSGetUptimeMs())) {
            handle->port_error = A_STATUS_TIMEOUT;
            return SFUD_ERR_TIMEOUT;
        }
        status = aDevFlash25qSpiTransfer(handle, tx, tx_size, rx, rx_size);
        if (status != A_STATUS_OK) {
            handle->port_error = status;
            return SFUD_ERR_WRITE;
        }
        if (!status_read || ((rx[0] & SFUD_STATUS_REGISTER_BUSY) == 0U))
            return SFUD_SUCCESS;
        /* WIP 轮询不持有总线锁；同一调用始终复用 deadline。 */
        if (aTimepointExpired(&handle->deadline, aOSGetUptimeMs())) {
            handle->port_error = A_STATUS_TIMEOUT;
            return SFUD_ERR_TIMEOUT;
        }
        aOSDelayMs(1U);
    }
}

sfud_err sfud_spi_port_init(sfud_flash *flash)
{
    if (flash->user_data == NULL) return SFUD_ERR_NOT_FOUND;
    flash->spi.user_data = flash->user_data;
    flash->spi.wr = write_read;
    /* 外层已持有模块锁；void 锁回调不能表达加锁失败。 */
    flash->spi.lock = NULL;
    flash->spi.unlock = NULL;
    /* Busy polling is budgeted in write_read, including transport errors. */
    flash->retry.times = 0U;
    flash->retry.delay = NULL;
    return SFUD_SUCCESS;
}
