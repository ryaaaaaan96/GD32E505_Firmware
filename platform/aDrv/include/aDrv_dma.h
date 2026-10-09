/**
 * @file aDrv_dma.h
 * @brief Dma 非阻塞硬件操作接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 配置和生命周期由上层管理，同一实例的操作由调用者串行化。
 * 句柄字段是驱动维护状态，初始化后禁止上层修改；本层不依赖 aOS。
 * 地址和长度必须在通道关闭时配置；Enable 只启动硬件，不等待传输完成。
 * 缓冲区必须可被 DMA 访问并满足数据宽度对齐；活动期间不得释放或改写源数据。
 * 事件回调仅在 DMA ISR 执行；本接口不执行 cache 维护或软件等待。
 */

#ifndef ADRV_DMA_H
#define ADRV_DMA_H

#include "aDrv_basic.h"

/** @brief DMA 搬运方向；地址设置接口始终绑定 memory/peripheral 两侧。 */
typedef enum {
    ADRV_DMA_DIR_PERIPH_TO_MEMORY,
    ADRV_DMA_DIR_MEMORY_TO_PERIPH,
    ADRV_DMA_DIR_MEMORY_TO_MEMORY,
} aDrvDmaDirection_t;

/** @brief 单个传输单元的位宽，不是传输数量。 */
typedef enum {
    ADRV_DMA_WIDTH_8,
    ADRV_DMA_WIDTH_16,
    ADRV_DMA_WIDTH_32,
} aDrvDmaWidth_t;

/** @brief DMA 仲裁优先级，从 LOW 到 ULTRA；与 NVIC IRQ 优先级无关。 */
typedef enum {
    ADRV_DMA_PRIORITY_LOW,
    ADRV_DMA_PRIORITY_MEDIUM,
    ADRV_DMA_PRIORITY_HIGH,
    ADRV_DMA_PRIORITY_ULTRA,
} aDrvDmaPriority_t;

/** @brief aDrvDmaConfig_t 配置描述；初始化/注册时读取，借用对象的生命周期见对应接口。 */
typedef struct {
    /** 逻辑 DMA 通道，GD32E505 的 0..6 为 DMA0，7..11 为 DMA1。 */
    aDrvDmaChannel_t channel;
    /**< 传输方向，不改变地址 setter 对 memory/peripheral 的绑定。 */
    aDrvDmaDirection_t direction;
    aDrvDmaWidth_t periphWidth; /**< peripheral 侧传输宽度。 */
    aDrvDmaWidth_t memoryWidth; /**< memory 侧传输宽度。 */
    aDrvDmaPriority_t priority; /**< DMA 通道仲裁优先级，不是 NVIC 优先级。 */
    aBool_t periphIncrement; /**< 每次传输是否递增 peripheral 地址。 */
    aBool_t memoryIncrement; /**< 每次传输是否递增 memory 地址。 */
    aBool_t circular; /**< 完成后是否重装地址和数量。 */
} aDrvDmaConfig_t;

/** @brief 通道事件位，可组合；完成表示 DMA 搬运结束，不是外设线路空闲。 */
typedef enum {
    ADRV_DMA_EVENT_HALF = 1U << 0,
    ADRV_DMA_EVENT_COMPLETE = 1U << 1,
    ADRV_DMA_EVENT_ERROR = 1U << 2,
} aDrvDmaEvent_t;

/** @brief ISR 通知，不得阻塞；argument 借用至注销且在途回调退出。 */
typedef void (*aDrvDmaCallback_t)(void *argument, uint32_t events);

/** @brief 注册时复制，允许配置对象位于调用栈。 */
typedef struct {
    aDrvDmaCallback_t callback;
    void *argument;
    uint32_t events;
    uint8_t priority; /**< GD32 未移位的 NVIC 优先级，0..15。 */
} aDrvDmaInterruptConfig_t;

/** @brief 默认 COMPLETE/ERROR 事件、优先级 5；使用前填写 callback。 */
void aDrvDmaInterruptConfigStructInit(aDrvDmaInterruptConfig_t *config);

/** @brief 一次一致的搬运快照，数量单位为配置的数据宽度。 */
typedef struct {
    size_t transferred; /**< 从本次 Enable 起累计；size_t 自然回绕。 */
    uint32_t remaining; /**< 当前块剩余数量。 */
} aDrvDmaProgress_t;

/** @brief aDrvDmaHandle_t 驱动/设备状态；调用方提供存储，字段仅由所属模块维护。 */
typedef struct {
    uintptr_t controller; /**< 内部 DMA 控制器地址，不是配置项。 */
    uint8_t channel; /**< 控制器内硬件通道号，不是逻辑 channel。 */
    aBool_t initialized; /**< 是否已取得并配置通道。 */
    aBool_t circular;
    aBool_t error;
    uint32_t length;
    size_t cycles;
    uint32_t pending_events;
    aDrvDmaInterruptConfig_t interrupt;
} aDrvDmaHandle_t;

/** 配置 DMA 事件；NULL 注销。调用方串行化生命周期和在途回调。
 * 不清除已发生的硬件事件，支持启动后注册；回调只在 ISR 派发。 */
aStatus_t aDrvDmaConfigureInterrupt(
    aDrvDmaHandle_t *handle, const aDrvDmaInterruptConfig_t *config);

/** 读取进度并保留待派发事件，不在调用线程执行回调。
 * ERROR 表示本次传输曾发生错误；BUSY 表示快照跨越重装，输出不可用。
 * 循环 DMA 必须在一整圈内服务完成标志，多圈未服务无法恢复真实圈数。 */
aStatus_t aDrvDmaGetProgress(aDrvDmaHandle_t *handle,
                            aDrvDmaProgress_t *progress);

/**
 * @brief 填充默认配置，不访问硬件。
 * @param[out] config 配置对象；NULL 时不操作。
 * @note 引脚/通道等板级资源需在初始化前由调用者补齐。
 */
void aDrvDmaConfigStructInit(aDrvDmaConfig_t *config);

/**
 * @brief 将句柄重置为未初始化状态。
 * @param[out] handle 调用方存储；NULL 时不操作。
 * @warning 仅用于未使用的句柄；不是反初始化函数，不能清除活动资源的所有权。
 */
void aDrvDmaHandleStructInit(aDrvDmaHandle_t *handle);

/**
 * @brief 配置并占用逻辑 DMA 通道，尚不启动传输。
 * @param[in] config 方向、宽度、递增和循环配置。
 * @param[out] handle 调用方稳定存储的句柄；活动期间不得复制或移动。
 * @retval A_STATUS_OK 初始化成功。
 * @retval A_STATUS_BUSY 通道已被其他句柄占用。
 * @retval A_STATUS_INVALID_PARAM 参数无效。
 */
aStatus_t aDrvDmaInitStatic(const aDrvDmaConfig_t *config,
                            aDrvDmaHandle_t *handle);

/**
 * @brief 停止 DMA、释放通道所有权并清空句柄。
 * @param[in,out] handle 初始化时使用的原始句柄。
 * @retval A_STATUS_OK 已释放。
 * @retval A_STATUS_ERROR 句柄与记录的通道所有者不匹配。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvDmaDeInitStatic(aDrvDmaHandle_t *handle);

/**
 * @brief 设置 DMA 的 memory 侧地址。
 * @param[in,out] handle 已初始化且通道已关闭的句柄。
 * @param[in] source memory 侧地址，不得为 NULL。
 * @retval A_STATUS_OK 地址已配置。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @warning 当前名称不表示实际传输方向；外设到内存时，此地址是接收目标。
 */
aStatus_t aDrvDmaSrcBufferSet(aDrvDmaHandle_t *handle, const void *source);

/**
 * @brief 设置 DMA 的 peripheral 侧地址。
 * @param[in,out] handle 已初始化且通道已关闭的句柄。
 * @param[in] destination peripheral 侧地址，不得为 NULL。
 * @retval A_STATUS_OK 地址已配置。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @warning 外设到内存时，此地址实际为源；不能按函数名推断方向。
 */
aStatus_t aDrvDmaDstBufferSet(aDrvDmaHandle_t *handle, void *destination);

/**
 * @brief 设置 DMA 传输单元数量。
 * @param[in,out] handle 已初始化且通道已关闭的句柄。
 * @param[in] length 大于 0，必须适合芯片计数器；GD32 上限 65535。
 * @retval A_STATUS_OK 数量已配置。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 单位是传输单元，只有 8 位宽度时等于字节；上限由调用方保证。
 */
aStatus_t aDrvDmaDstBufferLen(aDrvDmaHandle_t *handle, uint32_t length);

/**
 * @brief 关闭 DMA 通道。
 * @param[in,out] handle 已初始化句柄。
 * @retval A_STATUS_OK 已关闭；不返回完成字节数。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvDmaTransDisable(aDrvDmaHandle_t *handle);

/**
 * @brief 使能已经配置好地址和长度的 DMA 通道。
 * @param[in,out] handle 已初始化句柄。
 * @retval A_STATUS_OK 已使能，不代表传输完成。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvDmaTransEnable(aDrvDmaHandle_t *handle);

/**
 * @brief 配置传输结束后是否自动重装。
 * @param[in,out] handle 已初始化且通道已关闭的句柄。
 * @param[in] enabled A_TRUE 循环，A_FALSE 单次。
 * @retval A_STATUS_OK 模式已配置。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvDmaCircularSet(aDrvDmaHandle_t *handle, aBool_t enabled);

/**
 * @brief 读取 DMA 剩余传输单元数。
 * @param[in] handle 已初始化句柄。
 * @return 当前计数；空或未初始化句柄返回 0，不能据此区分完成与无效句柄。
 */
uint32_t aDrvDmaCurLenGet(const aDrvDmaHandle_t *handle);

#endif
