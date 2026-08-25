#include "aDev_flash25q.h"

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
#define FLASH_OPERATION_TIMEOUT_MS 5000U
#define FLASH_CHIP_ERASE_TIMEOUT_MS 120000U

static aDevFlash25qHandle_t *s_flash_devices[ADEV_FLASH25Q_DEVICE_COUNT];

static aStatus_t issue_command(aDevFlash25qHandle_t *handle,
                                  uint32_t instruction, uint32_t address,
                                  uint32_t length, uint32_t functional_mode,
                                  uint32_t dummy_cycles, bool has_address)
{
    aDrvQspiCmd_t command = {0};
    command.Instruction = instruction;
    command.InstructionMode = ADRV_QSPI_INST_1_LINE;
    command.Address = address;
    command.AddressSize = 24U;
    command.AddressMode = has_address ? ADRV_QSPI_ADDR_1_LINE : ADRV_QSPI_ADDR_NONE;
    command.DataMode = length == 0U ? ADRV_QSPI_DATA_NONE : ADRV_QSPI_DATA_1_LINE;
    command.NbData = length;
    command.DummyCycles = dummy_cycles;
    command.FunctionalMode = functional_mode;
    return aDrvQspiCommand(&handle->qspi, &command);
}

static aStatus_t write_enable(aDevFlash25qHandle_t *handle)
{
    return issue_command(handle, FLASH_CMD_WRITE_ENABLE, 0U, 0U,
                         ADRV_QSPI_FMODE_INDIRECT_WRITE, 0U, false);
}

static aStatus_t wait_ready(aDevFlash25qHandle_t *handle, uint32_t timeout_ms)
{
    for (uint32_t elapsed_ms = 0U; elapsed_ms <= timeout_ms; ++elapsed_ms) {
        uint8_t status_register = 0U;
        aStatus_t status = issue_command(handle, FLASH_CMD_READ_STATUS,
                                            0U, 1U,
                                            ADRV_QSPI_FMODE_INDIRECT_READ,
                                            0U, false);
        if (status == A_STATUS_OK) {
            status = aDrvQspiReceive(&handle->qspi, &status_register, 1U);
        }
        if (status != A_STATUS_OK) return status;
        if ((status_register & FLASH_BUSY_MASK) == 0U) return A_STATUS_OK;
        if (elapsed_ms == timeout_ms) break;
        aDrvDelayMs(1U);
    }
    return A_STATUS_TIMEOUT;
}

void aDevFlash25qConfigStructInit(aDevFlash25qConfig_t *config)
{
    if (config == NULL) return;
    aDrvQspiConfigStructInit(&config->drv_config);
    config->flashIndex = 0U;
    config->capacity = 16U * 1024U * 1024U;
}

void aDevFlash25qHandleStructInit(aDevFlash25qHandle_t *handle)
{
    if (handle == NULL) return;
    aDrvQspiHandleStructInit(&handle->qspi);
    handle->size = 0U;
    handle->flash_index = 0U;
    handle->init_ok = 0U;
    handle->fast_read = 0U;
}

aStatus_t aDevFlash25qInit(const aDevFlash25qConfig_t *config,
                              aDevFlash25qHandle_t *handle)
{
    if ((config == NULL) || (handle == NULL) || (config->capacity == 0U) ||
        (config->flashIndex >= ADEV_FLASH25Q_DEVICE_COUNT)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (s_flash_devices[config->flashIndex] != NULL) return A_STATUS_BUSY;
    aDevFlash25qHandleStructInit(handle);
    const aStatus_t status = aDrvQspiInitStatic(&config->drv_config, &handle->qspi);
    if (status != A_STATUS_OK) return status;
    handle->size = config->capacity;
    handle->flash_index = config->flashIndex;
    handle->init_ok = 1U;
    s_flash_devices[config->flashIndex] = handle;
    return A_STATUS_OK;
}

aStatus_t aDevFlash25qDeInit(aDevFlash25qHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->init_ok == 0U) return A_STATUS_NOT_READY;
    const uint8_t flash_index = handle->flash_index;
    const aStatus_t status = aDrvQspiDeInitStatic(&handle->qspi);
    if ((flash_index < ADEV_FLASH25Q_DEVICE_COUNT) &&
        (s_flash_devices[flash_index] == handle)) {
        s_flash_devices[flash_index] = NULL;
    }
    aDevFlash25qHandleStructInit(handle);
    return status;
}

aStatus_t aDevFlash25qRead(aDevFlash25qHandle_t *handle, uint32_t address,
                              uint8_t *data, uint32_t size)
{
    if ((handle == NULL) || (data == NULL) || (size == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->init_ok == 0U) return A_STATUS_NOT_READY;
    if ((address > handle->size) || (size > (handle->size - address))) {
        return A_STATUS_INVALID_PARAM;
    }
    aStatus_t status = issue_command(handle, handle->fast_read != 0U ? FLASH_CMD_FAST_READ : FLASH_CMD_READ,
                                        address, size, ADRV_QSPI_FMODE_INDIRECT_READ,
                                        handle->fast_read != 0U ? 8U : 0U, true);
    return status == A_STATUS_OK ? aDrvQspiReceive(&handle->qspi, data, size) : status;
}

aStatus_t aDevFlash25qWrite(aDevFlash25qHandle_t *handle, uint32_t address,
                               const uint8_t *data, uint32_t size)
{
    if ((handle == NULL) || (data == NULL) || (size == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->init_ok == 0U) return A_STATUS_NOT_READY;
    if ((address > handle->size) || (size > (handle->size - address))) {
        return A_STATUS_INVALID_PARAM;
    }
    uint32_t written = 0U;
    while (written < size) {
        uint32_t chunk = FLASH_PAGE_SIZE - ((address + written) % FLASH_PAGE_SIZE);
        if (chunk > (size - written)) chunk = size - written;
        aStatus_t status = write_enable(handle);
        if (status == A_STATUS_OK) {
            status = issue_command(handle, FLASH_CMD_PAGE_PROGRAM, address + written,
                                   chunk, ADRV_QSPI_FMODE_INDIRECT_WRITE, 0U,
                                   true);
        }
        if (status == A_STATUS_OK) status = aDrvQspiTransmit(&handle->qspi, &data[written], chunk);
        if (status == A_STATUS_OK) status = wait_ready(handle, FLASH_OPERATION_TIMEOUT_MS);
        if (status != A_STATUS_OK) return status;
        written += chunk;
    }
    return A_STATUS_OK;
}

aStatus_t aDevFlash25qErase(aDevFlash25qHandle_t *handle, uint32_t address,
                               uint32_t size)
{
    if ((handle == NULL) || (size == 0U) ||
        ((address % FLASH_SECTOR_SIZE) != 0U) ||
        ((size % FLASH_SECTOR_SIZE) != 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->init_ok == 0U) return A_STATUS_NOT_READY;
    if ((address > handle->size) || (size > (handle->size - address))) {
        return A_STATUS_INVALID_PARAM;
    }
    for (uint32_t offset = 0U; offset < size; offset += FLASH_SECTOR_SIZE) {
        aStatus_t status = write_enable(handle);
        if (status == A_STATUS_OK) {
            status = issue_command(handle, FLASH_CMD_SECTOR_ERASE,
                                   address + offset, 0U,
                                   ADRV_QSPI_FMODE_INDIRECT_WRITE, 0U, true);
        }
        if (status == A_STATUS_OK) status = wait_ready(handle, FLASH_OPERATION_TIMEOUT_MS);
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
}

aStatus_t aDevFlash25qChipErase(aDevFlash25qHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->init_ok == 0U) return A_STATUS_NOT_READY;
    aStatus_t status = write_enable(handle);
    if (status == A_STATUS_OK) {
        status = issue_command(handle, FLASH_CMD_CHIP_ERASE, 0U, 0U,
                               ADRV_QSPI_FMODE_INDIRECT_WRITE, 0U, false);
    }
    return status == A_STATUS_OK ?
           wait_ready(handle, FLASH_CHIP_ERASE_TIMEOUT_MS) : status;
}

uint32_t aDevFlash25qGetSize(const aDevFlash25qHandle_t *handle)
{
    return handle == NULL ? 0U : handle->size;
}

aStatus_t aDevFlash25qHandleIsValid(const aDevFlash25qHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    return handle->init_ok != 0U ? A_STATUS_OK : A_STATUS_NOT_READY;
}

aStatus_t aDevFlash25qIoCtl(aDevFlash25qHandle_t *handle, uint32_t command,
                               void *argument)
{
    if ((handle == NULL) ||
        (command != ADEV_FLASH_IOCTL_QSPI_FAST_READ)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->init_ok == 0U) return A_STATUS_NOT_READY;
    handle->fast_read = (uint8_t)((uintptr_t)argument != 0U ? 1U : 0U);
    return A_STATUS_OK;
}

aDevFlash25qHandle_t *aDevFlash25qGetDevice(uint8_t flash_index)
{
    if (flash_index >= ADEV_FLASH25Q_DEVICE_COUNT) return NULL;
    return s_flash_devices[flash_index];
}
