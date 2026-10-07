#include "app_system_flash.h"
#include "aDev_flash25q_instance.h"
#if APP_DATABASE_ENABLE
#include "aDataBase.h"
#endif
#if AMEMORY_ENABLE
#include "app_system_memory.h"
#endif
#if ASHELL_ENABLE
#include "aShell.h"
#endif

static aDevFlash25qBus_t flash_bus;
static aDevFlash25qHandle_t *flash_handle;
#if !ADEV_FLASH25Q_DYNAMIC_ENABLE
static aDevFlash25qHandle_t flash_instance;
#endif

aStatus_t appSystemFlashInit(void)
{
    aDevFlash25qBusConfig_t bus_config;
    aDevFlash25qConfig_t config;
    aStatus_t status;
#if ASHELL_ENABLE
    aDevFlash25qInfo_t info;
#endif

    aDevFlash25qBusConfigStructInit(&bus_config);
    /* 芯片 SPI1：PB13=SCK，PB14=MISO，PB15=MOSI。 */
    bus_config.spi.spiId = ADRV_SPI_1;
    bus_config.spi.sckPin = ADRV_PIN(ADRV_GPIO_PORT_B, 13);
    bus_config.spi.misoPin = ADRV_PIN(ADRV_GPIO_PORT_B, 14);
    bus_config.spi.mosiPin = ADRV_PIN(ADRV_GPIO_PORT_B, 15);
    status = aDevFlash25qBusInitStatic(&bus_config, &flash_bus);
    if (status != A_STATUS_OK) return status;

    aDevFlash25qConfigStructInit(&config);
    config.bus = &flash_bus;
    config.cs_pin = ADRV_PIN(ADRV_GPIO_PORT_B, 12);
    /* 型号与容量由 SFUD 探测，启动不擦除或编程数据。 */
#if ADEV_FLASH25Q_DYNAMIC_ENABLE
    status = aDevFlash25qCreate(&config, &flash_handle);
#else
    status = aDevFlash25qInitStatic(&config, &flash_instance);
    if (status == A_STATUS_OK) flash_handle = &flash_instance;
#endif
    if (status != A_STATUS_OK) {
        (void)aDevFlash25qBusDeInitStatic(&flash_bus);
#if ASHELL_ENABLE
        ASHELL_PRINT("SPI1 Flash init failed: %d\r\n", (int)status);
#endif
        return status;
    }
#if ASHELL_ENABLE
    status = aDevFlash25qGetInfo(flash_handle, &info);
    if (status == A_STATUS_OK) {
        ASHELL_PRINT("SPI1 Flash: JEDEC %02X %02X %02X, "
                     "%lu bytes, erase %lu bytes\r\n",
                     (unsigned)info.manufacturer_id,
                     (unsigned)info.memory_type,
                     (unsigned)info.capacity_id,
                     (unsigned long)info.capacity,
                     (unsigned long)info.erase_size);
    }
#endif
#if AMEMORY_ENABLE
    status = appSystemMemoryInit();
    if (status != A_STATUS_OK) goto fail;
#endif
#if APP_DATABASE_ENABLE
    status = aDataBaseInit();
    if (status != A_STATUS_OK) {
        (void)aMemoryDeInit();
        goto fail;
    }
#endif
    return A_STATUS_OK;
#if AMEMORY_ENABLE
fail:
#if ADEV_FLASH25Q_DYNAMIC_ENABLE
    (void)aDevFlash25qDestroy(flash_handle);
#else
    (void)aDevFlash25qDeInitStatic(flash_handle);
#endif
    flash_handle = NULL;
    (void)aDevFlash25qBusDeInitStatic(&flash_bus);
    return status;
#endif
}

aStatus_t appSystemFlashGetInfo(aDevFlash25qInfo_t *info)
{
    if (flash_handle == NULL) return A_STATUS_NOT_READY;
    return aDevFlash25qGetInfo(flash_handle, info);
}

aStatus_t appSystemFlashRead(const aDevFlash25qReadRequest_t *request)
{
    if (flash_handle == NULL) return A_STATUS_NOT_READY;
    return aDevFlash25qRead(flash_handle, request);
}

aStatus_t appSystemFlashWrite(const aDevFlash25qWriteRequest_t *request)
{
    if (flash_handle == NULL) return A_STATUS_NOT_READY;
    return aDevFlash25qWrite(flash_handle, request);
}

aStatus_t appSystemFlashErase(const aDevFlash25qEraseRequest_t *request)
{
    if (flash_handle == NULL) return A_STATUS_NOT_READY;
    return aDevFlash25qErase(flash_handle, request);
}
