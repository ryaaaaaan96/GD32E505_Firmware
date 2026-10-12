/**
 * @file aDrv_dma_memory.h
 * @brief DMA 缓冲区检查与 CPU/DMA 数据交接，不分配内存、不依赖 aOS。
 *
 * aDev 在数据交接处调用缓存接口；直接使用 aDrv DMA 时由调用者负责。
 * 循环 RX 只接受无需缓存维护的区域，检查通过后不调用 RxPrepare/RxComplete。
 * 先检查缓冲区，再进行缓存维护；活动期间不得释放或修改 DMA 使用的数据。
 * 缓存操作须可在 ISR 调用，不阻塞；不替代 DMA 启停的内存屏障。
 */
#ifndef ADRV_DMA_MEMORY_H
#define ADRV_DMA_MEMORY_H

#include "aLib.h"

typedef enum {
    ADRV_DMA_BUFFER_TX,          /**< DMA 读取；允许平台支持的只读内存。 */
    ADRV_DMA_BUFFER_TX_RING,     /**< CPU 填充、DMA 读取；必须可写。 */
    ADRV_DMA_BUFFER_RX,          /**< DMA 写入；须可写且满足缓存行隔离要求。 */
    ADRV_DMA_BUFFER_RX_CIRCULAR, /**< 持续写入；仅接受无需缓存维护的区域。 */
} aDrvDmaBufferUsage_t;

/** 检查完整字节范围及用途；空指针、零长度、不可达区域返回 INVALID_PARAM。
 * 平台不支持的缓存策略返回 UNSUPPORTED；不访问数据、不修改硬件。
 * GD32 当前允许链接脚本中的 RAM，仅普通 TX 允许主 Flash；未接入外部存储区。
 * 有缓存的平台必须检查 RX 缓存行隔离，不能向外取整后破坏相邻业务数据。
 * 不验证对象实际容量/生命周期；传输宽度对齐另由 DMA 地址设置接口检查。 */
aStatus_t aDrvDmaBufferCheck(const void *buffer, size_t size,
                            aDrvDmaBufferUsage_t usage);

/* 仅平台后端可声明此能力；未知平台必须实现函数，不能默认跳过维护。
 * GD32E505 的 SRAM 无需缓存维护；FMC 的 Flash 读缓存不属于此接口。 */
#if defined(ADRV_DMA_CACHE_COHERENT) && ADRV_DMA_CACHE_COHERENT
static inline void aDrvDmaTxPrepare(const void *buffer, size_t size)
{
    (void)buffer;
    (void)size;
}

static inline void aDrvDmaRxPrepare(void *buffer, size_t size)
{
    (void)buffer;
    (void)size;
}

static inline void aDrvDmaRxComplete(void *buffer, size_t size)
{
    (void)buffer;
    (void)size;
}
#else
/** CPU 写入结束后、启动 DMA 前，将待发送数据同步到内存。 */
void aDrvDmaTxPrepare(const void *buffer, size_t size);
/** DMA 开始写入前处理旧缓存，避免脏行之后覆盖 DMA 数据。 */
void aDrvDmaRxPrepare(void *buffer, size_t size);
/** DMA 停止写入后、CPU 读取或回调前，同步完整接收保留区。
 * 完成、超时、取消及启动失败均须归还保留区；循环 RX 不调用此接口。 */
void aDrvDmaRxComplete(void *buffer, size_t size);
#endif

/* 仅用于文件作用域的无初值字节数组；容量仍由应用指定。
 * section 本身不改变缓存属性；平台须配套链接布局、启动清零和 MPU 配置。
 * 带缓存的平台应提供符合缓存行/MPU 约束的对齐值及区域策略。 */
#if defined(ADRV_DMA_BUFFER_ALIGNMENT) && defined(__GNUC__)
#define ADRV_DMA_BUFFER \
    __attribute__((section(".dma_buffer"), \
                   aligned(ADRV_DMA_BUFFER_ALIGNMENT)))
#endif

#endif
