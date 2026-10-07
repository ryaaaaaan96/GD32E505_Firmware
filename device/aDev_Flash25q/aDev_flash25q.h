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

/** 多个 Flash 实例共享的 SPI 总线；片选由各 Flash 实例分别管理。
 * 当前支持 8 位 SPI 主机、软件 NSS、高位先传输；尚未实现 QSPI 后端。 */
typedef struct {
    aDrvSpiConfig_t spi;
} aDevFlash25qBusConfig_t;

typedef struct {
    aDevFlash25qBus_t *bus; /**< 借用总线，直到实例反初始化或销毁。 */
    aDrvGpioPin_t cs_pin; /**< 低电平有效的片选引脚。 */
    uint32_t expected_capacity; /**< 预期容量，单位字节；零表示接受探测容量。 */
    aTimeout_t timeout; /**< 初始化的总超时时间。 */
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

/** 所有实例的生命周期操作必须由外部统一串行化，并在任务中调用。
 * 初始化、反初始化和销毁不得与任何接口调用并发执行。
 * 每个物理控制器只创建一个总线对象；总线上所有芯片使用相同的 SPI 模式。
 * 总线对象的静态存储类型由 aDev_flash25q_instance.h 提供。
 * 仍有 Flash 实例借用总线时，总线反初始化返回 A_STATUS_BUSY。 */
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

/** 同步读写接口，仅允许在任务中调用。
 * SFUD 官方实现共享页缓冲区，因此所有实例的 SFUD 调用统一串行化。
 * 超时包含等待锁的时间；支持正数有限超时和永久等待，不支持立即返回。
 * 写入不会自动擦除；擦除地址和长度必须与擦除单元严格对齐。
 * 失败时数据可能已被部分修改，不提供回滚或自动读回校验。
 * 总线故障后，必须先关闭其全部实例，再重新初始化总线。
 * 缓冲区仅在调用期间借用；禁止复制或移动已经初始化的对象。
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
