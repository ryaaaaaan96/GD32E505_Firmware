#ifndef ADEV_FLASH25Q_H
#define ADEV_FLASH25Q_H

#include "aDrv_spi.h"

#ifndef ADEV_FLASH25Q_STATIC_ENABLE
#define ADEV_FLASH25Q_STATIC_ENABLE 1
#endif
#ifndef ADEV_FLASH25Q_DYNAMIC_ENABLE
#define ADEV_FLASH25Q_DYNAMIC_ENABLE 1
#endif

typedef struct aDevFlash25qBus aDevFlash25qBus_t;
typedef struct aDevFlash25qHandle aDevFlash25qHandle_t;

/** SPI bus shared by Flash instances. CS belongs to each Flash, not the bus.
 * First backend: 8-bit SPI master, software NSS, MSB. No QSPI backend yet. */
typedef struct {
    aDrvSpiConfig_t spi;
} aDevFlash25qBusConfig_t;

typedef struct {
    aDevFlash25qBus_t *bus; /**< Borrowed until DeInit/Destroy. */
    aDrvGpioPin_t cs_pin; /**< Active-low chip select. */
    uint32_t expected_capacity; /**< Bytes; zero accepts detected capacity. */
    aTimeout_t timeout; /**< Total initialization budget. */
} aDevFlash25qConfig_t;

typedef struct {
    uint32_t address;
    void *data;
    size_t size;
    aTimeout_t timeout;
} aDevFlash25qReadRequest_t;

typedef struct {
    uint32_t address;
    const void *data;
    size_t size;
    aTimeout_t timeout;
} aDevFlash25qWriteRequest_t;

typedef struct {
    uint32_t address;
    size_t size;
    aTimeout_t timeout;
} aDevFlash25qEraseRequest_t;

typedef struct {
    uint32_t capacity;
    uint32_t erase_size;
    uint8_t manufacturer_id;
    uint8_t memory_type;
    uint8_t capacity_id;
} aDevFlash25qInfo_t;

void aDevFlash25qBusConfigStructInit(aDevFlash25qBusConfig_t *config);
void aDevFlash25qConfigStructInit(aDevFlash25qConfig_t *config);
void aDevFlash25qReadRequestStructInit(aDevFlash25qReadRequest_t *request);
void aDevFlash25qWriteRequestStructInit(aDevFlash25qWriteRequest_t *request);
void aDevFlash25qEraseRequestStructInit(aDevFlash25qEraseRequest_t *request);

/** Lifecycle calls across ALL instances must be externally serialized.
 * Call from tasks; do not race any lifecycle operation with an API call.
 * One bus per physical controller. All chips on it use the same SPI mode.
 * Bus storage is declared through aDev_flash25q_instance.h.
 * Bus DeInit returns BUSY while any Flash instance borrows it. */
aStatus_t aDevFlash25qBusInitStatic(
    const aDevFlash25qBusConfig_t *config,
    aDevFlash25qBus_t *bus);
aStatus_t aDevFlash25qBusDeInitStatic(aDevFlash25qBus_t *bus);

#if ADEV_FLASH25Q_STATIC_ENABLE
aStatus_t aDevFlash25qInitStatic(
    const aDevFlash25qConfig_t *config,
    aDevFlash25qHandle_t *handle);
aStatus_t aDevFlash25qDeInitStatic(aDevFlash25qHandle_t *handle);
#endif
#if ADEV_FLASH25Q_DYNAMIC_ENABLE
aStatus_t aDevFlash25qCreate(
    const aDevFlash25qConfig_t *config,
    aDevFlash25qHandle_t **handle_out);
aStatus_t aDevFlash25qDestroy(aDevFlash25qHandle_t *handle);
#endif

/** Synchronous task-only operations. SFUD calls are globally serialized
 * because upstream uses a shared page buffer. Timeout includes lock wait.
 * NO_WAIT is unsupported; finite positive timeouts and FOREVER are accepted.
 * Write does not erase. Erase requires exact erase-unit alignment.
 * Failure may leave partially modified data; no rollback/readback guarantee.
 * On bus failure, all instances must be closed before bus reinitialization.
 * Buffers are borrowed only until return. Never copy/move initialized objects.
 */
aStatus_t aDevFlash25qRead(
    aDevFlash25qHandle_t *handle,
    const aDevFlash25qReadRequest_t *request);
aStatus_t aDevFlash25qWrite(
    aDevFlash25qHandle_t *handle,
    const aDevFlash25qWriteRequest_t *request);
aStatus_t aDevFlash25qErase(
    aDevFlash25qHandle_t *handle,
    const aDevFlash25qEraseRequest_t *request);
aStatus_t aDevFlash25qGetInfo(
    const aDevFlash25qHandle_t *handle,
    aDevFlash25qInfo_t *info);

#endif
