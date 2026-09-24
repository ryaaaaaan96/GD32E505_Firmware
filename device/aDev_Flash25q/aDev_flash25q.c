#include "aDev_flash25q.h"

#include "aOS.h"

#define FLASH_CMD_READ 0x03U
#define FLASH_CMD_FAST_READ 0x0BU
#define FLASH_CMD_PAGE_PROGRAM 0x02U
#define FLASH_CMD_WRITE_ENABLE 0x06U
#define FLASH_CMD_READ_STATUS 0x05U
#define FLASH_CMD_SECTOR_ERASE 0x20U
#define FLASH_CMD_CHIP_ERASE 0xC7U
#define FLASH_SECTOR_SIZE 4096U
#define FLASH_PAGE_SIZE 256U
#define FLASH_BUSY_MASK 0x01U
#define FLASH_MAX_ADDRESS_BYTES 0x01000000U

static aStatus_t operation_lock(aDevFlash25qHandle_t *handle,
                                aTimeout_t timeout, aTimepoint_t *end)
{
    aStatus_t status;

    if (!aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    *end = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(handle->operation_mutex,
                          aTimepointRemaining(end, aOSGetUptimeMs()));
    if ((status == A_STATUS_BUSY) &&
        !((timeout.type == A_TIMEOUT_TYPE_RELATIVE) &&
          (timeout.milliseconds == 0U))) {
        return A_STATUS_TIMEOUT;
    }
    return status;
}

static aStatus_t issue_command(aDevFlash25qHandle_t *handle,
                               uint32_t instruction, uint32_t address,
                               uint32_t length, uint32_t functional_mode,
                               uint32_t dummy_cycles, aBool_t has_address)
{
    aDrvQspiCmd_t command = {0};
    command.Instruction = instruction;
    command.InstructionMode = ADRV_QSPI_INST_1_LINE;
    command.Address = address;
    command.AddressSize = 24U;
    command.AddressMode =
        has_address ? ADRV_QSPI_ADDR_1_LINE : ADRV_QSPI_ADDR_NONE;
    command.DataMode =
        length == 0U ? ADRV_QSPI_DATA_NONE : ADRV_QSPI_DATA_1_LINE;
    command.NbData = length;
    command.DummyCycles = dummy_cycles;
    command.FunctionalMode = functional_mode;
    return aDrvQspiCommand(&handle->qspi, &command);
}

static aStatus_t wait_command_complete(aDevFlash25qHandle_t *handle,
                                       const aTimepoint_t *end)
{
    for (;;) {
        aBool_t complete;
        const aStatus_t status = aDrvQspiIsCommandComplete(
            &handle->qspi, &complete);

        if (status != A_STATUS_OK) {
            return status;
        }
        if (complete) {
            return A_STATUS_OK;
        }
        if (aOSPollWaitExpired(end)) {
            return A_STATUS_TIMEOUT;
        }
    }
}

static aStatus_t write_enable(aDevFlash25qHandle_t *handle,
                              const aTimepoint_t *end)
{
    const aStatus_t status = issue_command(
        handle, FLASH_CMD_WRITE_ENABLE, 0U, 0U,
        ADRV_QSPI_FMODE_INDIRECT_WRITE, 0U, A_FALSE);

    return status == A_STATUS_OK ? wait_command_complete(handle, end)
                                 : status;
}

static aStatus_t wait_ready(aDevFlash25qHandle_t *handle,
                            const aTimepoint_t *end)
{
    for (;;) {
        uint8_t status_register = 0U;
        aStatus_t status = issue_command(
            handle, FLASH_CMD_READ_STATUS, 0U, 1U,
            ADRV_QSPI_FMODE_INDIRECT_READ, 0U, A_FALSE);
        if (status == A_STATUS_OK) {
            status = aDrvQspiReceive(&handle->qspi, &status_register, 1U);
        }
        if (status != A_STATUS_OK) {
            return status;
        }
        if ((status_register & FLASH_BUSY_MASK) == 0U) {
            return A_STATUS_OK;
        }
        if (aTimepointExpired(end, aOSGetUptimeMs())) {
            return A_STATUS_TIMEOUT;
        }
        aOSDelayMs(1U);
    }
}

void aDevFlash25qConfigStructInit(aDevFlash25qConfig_t *config)
{
    if (config == NULL) return;
    aDrvQspiConfigStructInit(&config->drv_config);
    config->capacity = 16U * 1024U * 1024U;
}

void aDevFlash25qHandleStructInit(aDevFlash25qHandle_t *handle)
{
    if (handle == NULL) return;
    aDrvQspiHandleStructInit(&handle->qspi);
    handle->size = 0U;
    handle->operation_mutex = NULL;
    handle->initialized = A_FALSE;
    handle->fast_read = A_FALSE;
}

aStatus_t aDevFlash25qInit(const aDevFlash25qConfig_t *config,
                              aDevFlash25qHandle_t *handle)
{
    if ((config == NULL) || (handle == NULL) || (config->capacity == 0U) ||
        (config->capacity > FLASH_MAX_ADDRESS_BYTES)) {
        return A_STATUS_INVALID_PARAM;
    }
    aDevFlash25qHandleStructInit(handle);
    const aStatus_t status = aDrvQspiInitStatic(&config->drv_config, &handle->qspi);
    if (status != A_STATUS_OK) return status;
    if (aOSMutexCreate(&handle->operation_mutex) != A_STATUS_OK) {
        (void)aDrvQspiDeInitStatic(&handle->qspi);
        aDevFlash25qHandleStructInit(handle);
        return A_STATUS_NO_MEMORY;
    }
    handle->size = config->capacity;
    handle->initialized = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aDevFlash25qDeInit(aDevFlash25qHandle_t *handle)
{
    aStatus_t status;
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->initialized) return A_STATUS_NOT_READY;
    status = aOSMutexLock(handle->operation_mutex, A_TIMEOUT_NO_WAIT);
    if (status != A_STATUS_OK) return status;
    status = aDrvQspiDeInitStatic(&handle->qspi);
    (void)aOSMutexUnlock(handle->operation_mutex);
    if (status != A_STATUS_OK) return status;
    aOSMutexDestroy(&handle->operation_mutex);
    aDevFlash25qHandleStructInit(handle);
    return status;
}

aStatus_t aDevFlash25qRead(aDevFlash25qHandle_t *handle, uint32_t address,
                           uint8_t *data, uint32_t size,
                           aTimeout_t timeout)
{
    aTimepoint_t end;
    aStatus_t status;
    if ((handle == NULL) || (data == NULL) || (size == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->initialized) return A_STATUS_NOT_READY;
    if ((address > handle->size) || (size > (handle->size - address)) ||
        (address >= FLASH_MAX_ADDRESS_BYTES) ||
        (size > (FLASH_MAX_ADDRESS_BYTES - address))) {
        return A_STATUS_INVALID_PARAM;
    }
    status = operation_lock(handle, timeout, &end);
    if (status != A_STATUS_OK) return status;
    status = issue_command(handle,
                           handle->fast_read ? FLASH_CMD_FAST_READ : FLASH_CMD_READ,
                           address, size, ADRV_QSPI_FMODE_INDIRECT_READ,
                           handle->fast_read ? 8U : 0U, A_TRUE);
    if (status == A_STATUS_OK) status = wait_command_complete(handle, &end);
    if (status == A_STATUS_OK) status = aDrvQspiReceive(&handle->qspi, data, size);
    (void)aOSMutexUnlock(handle->operation_mutex);
    return status;
}

aStatus_t aDevFlash25qWrite(aDevFlash25qHandle_t *handle, uint32_t address,
                            const uint8_t *data, uint32_t size,
                            aTimeout_t timeout)
{
    aTimepoint_t end;
    aStatus_t status;

    if ((handle == NULL) || (data == NULL) || (size == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->initialized) return A_STATUS_NOT_READY;
    if ((address > handle->size) || (size > (handle->size - address)) ||
        (address >= FLASH_MAX_ADDRESS_BYTES) ||
        (size > (FLASH_MAX_ADDRESS_BYTES - address))) {
        return A_STATUS_INVALID_PARAM;
    }
    status = operation_lock(handle, timeout, &end);
    if (status != A_STATUS_OK) return status;

    uint32_t written = 0U;
    while (written < size) {
        uint32_t chunk = FLASH_PAGE_SIZE - ((address + written) % FLASH_PAGE_SIZE);
        if (chunk > (size - written)) chunk = size - written;
        status = write_enable(handle, &end);
        if (status == A_STATUS_OK) {
            status = issue_command(handle, FLASH_CMD_PAGE_PROGRAM, address + written,
                                   chunk, ADRV_QSPI_FMODE_INDIRECT_WRITE, 0U,
                                   A_TRUE);
        }
        if (status == A_STATUS_OK) status = aDrvQspiTransmit(&handle->qspi, &data[written], chunk);
        if (status == A_STATUS_OK) {
            status = wait_ready(handle, &end);
        }
        if (status != A_STATUS_OK) break;
        written += chunk;
    }
    (void)aOSMutexUnlock(handle->operation_mutex);
    return written == size ? A_STATUS_OK : status;
}

aStatus_t aDevFlash25qErase(aDevFlash25qHandle_t *handle, uint32_t address,
                            uint32_t size, aTimeout_t timeout)
{
    aTimepoint_t end;

    if ((handle == NULL) || (size == 0U) ||
        ((address % FLASH_SECTOR_SIZE) != 0U) ||
        ((size % FLASH_SECTOR_SIZE) != 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->initialized) return A_STATUS_NOT_READY;
    if ((address > handle->size) || (size > (handle->size - address)) ||
        (address >= FLASH_MAX_ADDRESS_BYTES) ||
        (size > (FLASH_MAX_ADDRESS_BYTES - address))) {
        return A_STATUS_INVALID_PARAM;
    }
    aStatus_t status = operation_lock(handle, timeout, &end);
    if (status != A_STATUS_OK) return status;

    for (uint32_t offset = 0U; offset < size; offset += FLASH_SECTOR_SIZE) {
        status = write_enable(handle, &end);
        if (status == A_STATUS_OK) {
            status = issue_command(handle, FLASH_CMD_SECTOR_ERASE,
                                   address + offset, 0U,
                                   ADRV_QSPI_FMODE_INDIRECT_WRITE, 0U, A_TRUE);
        }
        if (status == A_STATUS_OK) {
            status = wait_ready(handle, &end);
        }
        if (status != A_STATUS_OK) break;
    }
    (void)aOSMutexUnlock(handle->operation_mutex);
    return status;
}

aStatus_t aDevFlash25qChipErase(aDevFlash25qHandle_t *handle,
                                aTimeout_t timeout)
{
    aTimepoint_t end;

    if ((handle == NULL) || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return A_STATUS_NOT_READY;
    }

    aStatus_t status = operation_lock(handle, timeout, &end);
    if (status != A_STATUS_OK) return status;
    status = write_enable(handle, &end);
    if (status == A_STATUS_OK) {
        status = issue_command(handle, FLASH_CMD_CHIP_ERASE, 0U, 0U,
                               ADRV_QSPI_FMODE_INDIRECT_WRITE, 0U, A_FALSE);
    }
    if (status == A_STATUS_OK) {
        status = wait_command_complete(handle, &end);
    }
    if (status == A_STATUS_OK) status = wait_ready(handle, &end);
    (void)aOSMutexUnlock(handle->operation_mutex);
    return status;
}

uint32_t aDevFlash25qGetSize(const aDevFlash25qHandle_t *handle)
{
    return handle == NULL ? 0U : handle->size;
}

aStatus_t aDevFlash25qHandleIsValid(const aDevFlash25qHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    return handle->initialized ? A_STATUS_OK : A_STATUS_NOT_READY;
}

aStatus_t aDevFlash25qIoCtl(aDevFlash25qHandle_t *handle, uint32_t command,
                               void *argument)
{
    if ((handle == NULL) ||
        (command != ADEV_FLASH_IOCTL_QSPI_FAST_READ)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->initialized) return A_STATUS_NOT_READY;
    const aStatus_t status = aOSMutexLock(handle->operation_mutex,
                                          A_TIMEOUT_NO_WAIT);
    if (status != A_STATUS_OK) return status;
    handle->fast_read = (uintptr_t)argument != 0U;
    (void)aOSMutexUnlock(handle->operation_mutex);
    return A_STATUS_OK;
}
