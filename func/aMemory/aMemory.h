#ifndef AMEMORY_H
#define AMEMORY_H

#include "aLib.h"

#ifndef AMEMORY_MAX_PARTITIONS
#define AMEMORY_MAX_PARTITIONS 16U
#endif

#define AMEMORY_ACCESS_READ  (1U << 0)
#define AMEMORY_ACCESS_WRITE (1U << 1)
#define AMEMORY_ACCESS_ERASE (1U << 2)
#define AMEMORY_ACCESS_ALL   (AMEMORY_ACCESS_READ | \
    AMEMORY_ACCESS_WRITE | AMEMORY_ACCESS_ERASE)

typedef struct aMemoryHandle aMemoryHandle_t;

/** 公共接口地址相对分区；设备回调地址相对整个设备，均以字节计。
 * 缓冲区仅调用期间借用；成功必须完成全部请求，失败不承诺回滚。 */
typedef struct {
    uint64_t address;
    void *data;
    size_t size;
    aTimeout_t timeout;
} aMemoryReadRequest_t;

typedef struct {
    uint64_t address;
    const void *data;
    size_t size;
    aTimeout_t timeout;
} aMemoryWriteRequest_t;

typedef struct {
    uint64_t address;
    size_t size;
    aTimeout_t timeout;
} aMemoryEraseRequest_t;

typedef struct {
    uint64_t capacity;
    size_t read_granularity;  /**< 地址、长度粒度；不限制缓冲区对齐。 */
    size_t write_granularity; /**< 字节粒度，与页大小及编程位数不同。 */
    size_t erase_granularity; /**< 无擦除能力时为零。 */
    size_t program_bits;      /**< 编程粒度，单位位；NOR 通常为 1。 */
    uint8_t erased_value;     /**< 擦除后的字节值。 */
} aMemoryGeometry_t;

/** 同步、任务上下文回调；底层负责设备共享访问的同步。
 * RAM 写入完成即更新内存；持久化介质写入成功前必须完成实际提交。
 * write 不隐式擦除；不支持的操作填 NULL，返回 UNSUPPORTED。
 * 回调必须遵守传入超时，不得重入同一设备的不支持重入的驱动。 */
typedef struct {
    aStatus_t (*read)(void *context, const aMemoryReadRequest_t *request);
    aStatus_t (*write)(void *context, const aMemoryWriteRequest_t *request);
    aStatus_t (*erase)(void *context, const aMemoryEraseRequest_t *request);
} aMemoryOps_t;

typedef struct {
    const char *name;
    void *context;
    const aMemoryOps_t *ops;
    aMemoryGeometry_t geometry;
} aMemoryDevice_t;

typedef struct {
    const char *name;
    const aMemoryDevice_t *device;
    uint64_t offset;
    uint64_t size;
    uint32_t access;
} aMemoryPartition_t;

/** 所有表、名称、回调及上下文借用至 DeInit，期间禁止修改或移动。
 * 不拥有设备，应用先初始化设备并核对实际几何参数，再初始化本模块。 */
typedef struct {
    const aMemoryDevice_t *const *devices;
    size_t device_count;
    const aMemoryPartition_t *const *partitions;
    size_t partition_count;
} aMemoryConfig_t;

typedef struct {
    const char *name;
    const char *device_name;
    uint64_t offset;
    aMemoryGeometry_t geometry; /**< capacity 为分区容量。 */
    uint32_t access;
} aMemoryInfo_t;

/* 宏只声明只读配置；显式表注册无需链接器脚本，支持多设备及多分区。 */
#define AMEMORY_DEVICE_DEFINE(symbol, ...) \
    static const aMemoryDevice_t symbol = { __VA_ARGS__ }

#define AMEMORY_PARTITION_DEFINE(symbol, label, dev, start, length, rights) \
    static const aMemoryPartition_t symbol = { \
        .name = (label), .device = &(dev), .offset = (start), \
        .size = (length), .access = (rights) \
    }

void aMemoryConfigStructInit(aMemoryConfig_t *config);
void aMemoryReadRequestStructInit(aMemoryReadRequest_t *request);
void aMemoryWriteRequestStructInit(aMemoryWriteRequest_t *request);
void aMemoryEraseRequestStructInit(aMemoryEraseRequest_t *request);

/** 固定容量注册表，不分配堆；失败时不注册任何设备或分区。
 * 生命周期和 Retain/Release 由应用串行编排，不得与读写并发。
 * 分区不得重叠；同一物理设备只能注册一个设备对象。
 * DeInit 后所有借用句柄失效，调用者必须丢弃后重新查找。 */
aStatus_t aMemoryInit(const aMemoryConfig_t *config);
aStatus_t aMemoryDeInit(void);
const aMemoryHandle_t *aMemoryFind(const char *name);
aStatus_t aMemoryGetInfo(const aMemoryHandle_t *handle, aMemoryInfo_t *info);
/** 长期使用者持有模块引用，存在引用时 DeInit 返回 BUSY。 */
aStatus_t aMemoryRetain(void);
aStatus_t aMemoryRelease(void);

aStatus_t aMemoryRead(
    const aMemoryHandle_t *handle, const aMemoryReadRequest_t *request);
aStatus_t aMemoryWrite(
    const aMemoryHandle_t *handle, const aMemoryWriteRequest_t *request);
aStatus_t aMemoryErase(
    const aMemoryHandle_t *handle, const aMemoryEraseRequest_t *request);

#endif
