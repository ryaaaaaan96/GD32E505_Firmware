/**
 * @file aDrv_usart.h
 * @brief USART 非阻塞收发、事件与 DMA 操作接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * aDev 负责等待、超时与业务回调；此层仅配置/查询硬件，不依赖 aOS。
 * 句柄由上层持有，同一实例的生命周期和操作必须串行化；活动期间不得移动句柄。
 * Async 命名在此表示启动 DMA 硬件，不表示任务回调或业务请求队列。
 * INTERRUPT_ENABLE/DMA_ENABLE 为 0 时对应声明被裁剪，无 stub；调用方同步裁剪。
 * ADRV_USART_DMA_ENABLE 只控制硬件 DMA 操作，不启用 device 的业务异步 API。
 * DMA 启动前检查地址范围；缓存交接由调用者通过 aDrv_dma_memory.h 完成。
 * 直接调用本层的单次 RX，须先 Prepare，Stop/Abort 后 Complete 才能读数据。
 * 循环 RX 仅接受无需缓存维护的区域；进度/ISR 通知不表示整块所有权已交还。
 * 后端须串行派发同一实例的 USART/DMA 回调；除关闭外的操作不得阻塞，
 * 不在操作函数调用栈中直接调用登记回调，也不得持有回调需要再次获取的内部锁。
 * 当前 GD32 用单核、USART/DMA 同优先级实现派发顺序；SMP/线程后端尚未实现，
 * 移植时须同步硬件/登记状态并维持这些保证，不能依赖上层状态保护代替驱动同步。
 */

#ifndef ADRV_USART_H
#define ADRV_USART_H

#include "aDrv.h"
#include "aDrv_basic.h"

/** @brief CMake 导出的 0/1 编译能力；使用 #if 判断，不用 #ifdef。 */
#ifndef ADRV_USART_INTERRUPT_ENABLE
#define ADRV_USART_INTERRUPT_ENABLE 0
#endif

#ifndef ADRV_USART_DMA_ENABLE
#define ADRV_USART_DMA_ENABLE 0
#endif

/** @brief 逻辑 USART/UART 实例，枚举不保证存在相应引脚或 DMA 路由。 */
typedef enum {
    ADRV_USART_0,
    ADRV_USART_1,
    ADRV_USART_2,
    ADRV_USART_3,
    ADRV_USART_4,
    ADRV_USART_5,
} aDrvUsartId_t;

/** @brief USART 校验方式，默认 NONE。 */
typedef enum {
    ADRV_USART_PARITY_NONE,
    ADRV_USART_PARITY_EVEN,
    ADRV_USART_PARITY_ODD,
} aDrvUsartParity_t;

/** @brief 帧停止位数量，默认一个停止位。 */
typedef enum {
    ADRV_USART_STOP_1,
    ADRV_USART_STOP_2,
} aDrvUsartStopBits_t;

/** @brief aDrvUsartConfig_t 配置描述；初始化/注册时读取，借用对象的生命周期见对应接口。 */
typedef struct {
    aDrvUsartId_t id; /**< 逻辑 USART0..5，是否可用由芯片路由决定。 */
    uint32_t baud_rate; /**< 非零波特率，单位 bit/s。 */
    aDrvUsartParity_t parity; /**< 无/奇/偶校验。 */
    aDrvUsartStopBits_t stop_bits; /**< 1 或 2 个停止位。 */
    aDrvGpioPin_t tx_pin; /**< TX；USART2 按 PB10/PC10/PD8 自动重映射。 */
    aDrvGpioPin_t rx_pin; /**< RX 引脚，需满足当前 port 引脚映射。 */
    /** USART/DMA 共用中断优先级，默认 5；驱动初始化时校验并绑定。
     * GD32 为 0..15，且须满足平台构建传入的 OS ISR 安全范围。 */
    uint8_t interrupt_priority;
} aDrvUsartConfig_t;

/** @brief USART 驱动事件；当前 GD32 后端通过 IRQ 派发。 */
typedef enum {
    ADRV_USART_EVENT_TX_READY, /**< 可以继续提交发送数据，不是线路发完。 */
    ADRV_USART_EVENT_RX_READY, /**< 有接收数据可读。 */
    ADRV_USART_EVENT_TX_COMPLETE, /**< 最后一个停止位发送完成。 */
    ADRV_USART_EVENT_RX_IDLE, /**< 硬件空闲线检测，不等同于协议帧结束。 */
    ADRV_USART_EVENT_RX_ERROR, /**< 校验、帧、噪声或溢出等硬件异常通知。 */
    ADRV_USART_EVENT_DISPATCH, /**< RequestDispatch 请求的回调派发事件。 */
    ADRV_USART_EVENT_MAX, /**< 事件数量哨兵，不可作为注册事件。 */
} aDrvUsartEvent_t;

/** @brief 驱动事件回调；当前 GD32 后端在 USART ISR 执行，不得阻塞。
 * argument 由注册者提供，须保持有效至回调注销且在途调用退出。 */
typedef void (*aDrvUsartEventCallback_t)(void *argument);

/** @brief aDrvUsartCallback_t 配置描述；初始化/注册时读取，借用对象的生命周期见对应接口。 */
typedef struct {
    aDrvUsartEventCallback_t function; /**< 硬件 ISR 执行入口。 */
    void *argument; /**< 注册者借用参数，有效至在途回调退出。 */
} aDrvUsartCallback_t;

/**
 * @brief DMA 状态通知钩子，由 GD32 DMA ISR 调用。
 * @param[in] argument 启动 DMA 时提供的借用参数。
 * @note 用于设备层维护状态并唤醒任务；没有数据所有权转移，不可执行阻塞业务。
 */
typedef void (*aDrvUsartDmaCallback_t)(void *argument);

/** @brief 驱动内部路径占用位，可组合；上层不得直接改写。 */
typedef enum {
    ADRV_USART_OWNER_NONE = 0U,
    ADRV_USART_OWNER_INTERRUPT = 1U << 0,
    ADRV_USART_OWNER_ASYNC_TX = 1U << 1,
    ADRV_USART_OWNER_ASYNC_RX = 1U << 2,
} aDrvUsartOwner_t;

/** @brief aDrvUsartHandle_t 驱动/设备状态；调用方提供存储，字段仅由所属模块维护。 */
typedef struct {
    uintptr_t instance; /**< 内部 USART 寄存器地址。 */
    uint32_t baud_rate; /**< 当前波特率缓存，bit/s。 */
    aDrvUsartParity_t parity; /**< 当前校验方式缓存。 */
    aDrvUsartStopBits_t stop_bits; /**< 当前停止位缓存。 */
    aDrvUsartId_t id; /**< 本句柄绑定的逻辑实例。 */
    volatile aBool_t software_pending; /**< 待派发的软件事件。 */
    aDrvUsartCallback_t callbacks[ADRV_USART_EVENT_MAX]; /**< ISR 回调槽。 */
    aDrvUsartOwner_t owner; /**< 当前占用路径的位集合。 */
    uint32_t interrupt_enabled_mask; /**< 外设事件使能位，不等于 NVIC 状态。 */
    uint8_t irq_priority; /**< 初始化绑定的 USART/DMA 共用 NVIC 优先级。 */
    aBool_t initialized; /**< 基础 USART 配置是否完成。 */
} aDrvUsartHandle_t;

/** @brief aDrvUsartCallbackConfig_t 配置描述；初始化/注册时读取，借用对象的生命周期见对应接口。 */
typedef struct {
    aDrvUsartEvent_t event; /**< 要注册的 USART 驱动事件，不能为 EVENT_MAX。 */
    aDrvUsartEventCallback_t callback; /**< 必填 ISR 回调；不能阻塞。 */
    void *argument; /**< 原样传给 callback，可为 NULL。 */
    aBool_t enabled; /**< 登记回调时是否立即开启事件源。 */
} aDrvUsartCallbackConfig_t;

/**
 * @brief 填充默认 USART 配置。
 * @param[out] config 配置结构；NULL 不操作。
 * @note 初始化前必须根据板级连接设置 TX/RX 引脚，不能直接使用未填写的配置。
 */
void aDrvUsartConfigStructInit(aDrvUsartConfig_t *config);

/**
 * @brief 清空未使用的 USART 句柄。
 * @param[out] handle 调用方存储；NULL 不操作。
 * @warning 不是 DeInit；不得覆盖已注册到 IRQ/DMA 的活动句柄。
 */
void aDrvUsartHandleStructInit(aDrvUsartHandle_t *handle);

/**
 * @brief 配置 USART 并登记唯一实例句柄。
 * @param[in] config 本次调用期间有效的配置。
 * @param[out] handle 调用方持有的稳定存储，持续有效至 DeInit。
 * @retval A_STATUS_OK 已初始化。
 * @retval A_STATUS_INVALID_PARAM 参数、引脚、枚举或中断优先级不合法。
 * @retval A_STATUS_BUSY 实例已被其他句柄占用。
 * @note 不创建任务、等待对象或传输缓冲区。
 */
aStatus_t aDrvUsartInitStatic(const aDrvUsartConfig_t *config,
                              aDrvUsartHandle_t *handle);

/**
 * @brief 停止 USART/DMA、结束驱动回调访问并释放实例登记。
 * @param[in,out] handle 已初始化的原始句柄。
 * @retval A_STATUS_OK 已反初始化；驱动不再访问回调参数或 DMA 缓冲区。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @warning 仅任务/启动上下文；调用方独占生命周期，不得从驱动或业务回调中调用。
 *          等待时不得持有回调需要的保护；调用方另行停止软件定时器等外部访问源。
 * @note 当前单核后端关闭 USART/DMA 事件源并清除登记，任务关闭时无并行在途 ISR。
 *       将来 SMP/线程后端须在成功返回前等待在途驱动回调退出，当前未实现该后端。
 */
aStatus_t aDrvUsartDeInitStatic(aDrvUsartHandle_t *handle);

/**
 * @brief 尝试向 USART 数据寄存器写入一个字节。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] data 要发送的字节。
 * @retval A_STATUS_OK 已写寄存器，不代表最后停止位发完。
 * @retval A_STATUS_BUSY TX DMA 占用或硬件暂不可写。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvUsartTryWriteByte(aDrvUsartHandle_t *handle, uint8_t data);

/**
 * @brief 尝试从 USART 数据寄存器读取一个字节。
 * @param[in,out] handle 已初始化句柄。
 * @param[out] data 成功时写入收到的字节。
 * @retval A_STATUS_OK 已读一个字节。
 * @retval A_STATUS_BUSY RX DMA 占用或没有数据，输出不变。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvUsartTryReadByte(aDrvUsartHandle_t *handle, uint8_t *data);

/** 查询并清除接收硬件错误；ERROR 表示存在错误，丢弃关联数据字节。
 * 仅接收所有者调用；与 RXNE/IDLE/DMA 操作串行化。 */
aStatus_t aDrvUsartTakeRxError(aDrvUsartHandle_t *handle);

/**
 * @brief 查询 USART 的 TC 物理发送完成状态。
 * @param[in] handle 已初始化句柄。
 * @param[out] complete 成功时为 A_TRUE 表示 TC 已置位。
 * @retval A_STATUS_OK 状态查询成功，不是等待完成。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 不检查 aDev 软件 TX 缓冲区；软件尚未提交的数据不在该状态内。
 */
aStatus_t aDrvUsartIsTransmitComplete(
    const aDrvUsartHandle_t *handle, aBool_t *complete);
#if ADRV_USART_INTERRUPT_ENABLE
/** @brief 请求派发 DISPATCH 事件，可合并重复请求，不在本函数内直接调用回调。
 * 当前 GD32 后端通过挂起 USART IRQ 派发；事件须先注册并启用。
 * 回调可能在本函数返回之前执行。 */
aStatus_t aDrvUsartRequestDispatch(aDrvUsartHandle_t *handle);

/**
 * @brief 注册一个驱动事件回调并配置其使能状态。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] config 回调配置；复制字段，argument 对象须在回调使用期间有效。
 * @retval A_STATUS_OK 已登记。
 * @retval A_STATUS_BUSY TXE/RXNE 与活动 DMA 路径冲突。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @warning 当前 GD32 后端在 ISR 中回调，供 aDev 维护状态；回调不得阻塞。
 * @note 所有回调使用初始化时绑定的优先级，注册不修改该值。
 *       不在 PauseDispatch/ResumeDispatch 区间内注册；GD32 注册会恢复 USART IRQ。
 */
aStatus_t aDrvUsartRegisterCallback(
    aDrvUsartHandle_t *handle, const aDrvUsartCallbackConfig_t *config);

/**
 * @brief 关闭指定事件并清除其回调。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] event 要注销的事件。
 * @retval A_STATUS_OK 已注销；最后一个回调移除后关闭 NVIC。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 未拥有中断路径也返回 NOT_READY；调用方负责回调参数生命周期。
 */
aStatus_t aDrvUsartUnregisterCallback(aDrvUsartHandle_t *handle,
                                      aDrvUsartEvent_t event);

/**
 * @brief 启用或禁用已注册的 USART 事件，不移除回调。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] event 已注册回调的事件。
 * @param[in] enabled A_TRUE 使能，A_FALSE 屏蔽。
 * @retval A_STATUS_OK 已更新事件使能状态，当前 GD32 后端调整对应中断源。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 未注册该事件也返回 NOT_READY；上层仍须避免 TXE/RXNE 与 DMA 冲突。
 */
aStatus_t aDrvUsartSetEventEnabled(aDrvUsartHandle_t *handle,
                                   aDrvUsartEvent_t event,
                                   aBool_t enabled);

/**
 * @brief 查询本构建是否提供 USART 事件派发实现。
 * @return 编译该接口时返回 A_TRUE。
 * @note INTERRUPT_ENABLE 为 0 时不声明/链接此 API，不能用运行时查询代替编译裁剪。
 */
aBool_t aDrvUsartEventIsSupported(void);

/**
 * @brief 恢复通过 RegisterCallback 注册的 USART 事件派发。
 * @param[in,out] handle 已初始化且持有中断路径的句柄，否则不操作。
 * @note 当前 GD32 后端恢复 USART NVIC 中断线；不改变各事件的使能状态。
 *       与 PauseDispatch 配对使用，不记录嵌套层数或进入前状态，不控制 DMA 回调。
 */
void aDrvUsartResumeDispatch(aDrvUsartHandle_t *handle);

/**
 * @brief 暂停通过 RegisterCallback 注册的 USART 事件派发。
 * @param[in,out] handle 已初始化句柄；空或未初始化时不操作。
 * @note 当前 GD32 后端屏蔽 USART NVIC 中断线；不改变事件使能状态或清除待处理事件。
 *       不停止数据收发、不暂停 DMA 回调、不等待在途回调退出。
 *       仅供当前串行调用路径短暂暂停派发；不支持嵌套，不提供任务互斥。
 */
void aDrvUsartPauseDispatch(aDrvUsartHandle_t *handle);
#endif

#if ADRV_USART_DMA_ENABLE
/**
 * @brief 查询当前实例是否具有固定 TX DMA 路由。
 * @param[in] id USART 实例 ID；查询无需初始化，不访问硬件。
 * @return 有固定路由为 A_TRUE，无路由或 ID 无效为 A_FALSE。
 * @note 不保证通道当前空闲。
 */
aBool_t aDrvUsartDmaTxIsSupported(aDrvUsartId_t id);

/**
 * @brief 启动一段 DMA TX，不复制源数据、不等待完成。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] data DMA 可访问的源区域，直到完成/Abort 前不得修改或释放。
 * @param[in] size 请求字节数，大于 0。
 * @param[out] started 实际启动字节数，最多 65535，调用方处理余下分段。
 * @retval A_STATUS_OK DMA 已启动。
 * @retval A_STATUS_BUSY TXE 中断、当前 TX 或共享 DMA 资源占用。
 * @retval A_STATUS_UNSUPPORTED 实例无路由。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @return 也可能返回 DMA 初始化错误；完成使用 GetRemaining 查询，线路排空另查 TC。
 */
aStatus_t aDrvUsartAsyncTxStart(aDrvUsartHandle_t *handle,
                                const void *data, size_t size,
                                size_t *started);

/**
 * @brief 查询本段 TX DMA 剩余字节，并处理完成/错误状态。
 * @param[in,out] handle 已启动过 TX DMA 的句柄。
 * @param[out] remaining 剩余字节，0 仅表示 DMA 已搬运完，不等于 TC。
 * @retval A_STATUS_OK 查询成功。
 * @retval A_STATUS_ERROR DMA 传输错误。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 未建立 TX DMA 状态也返回 NOT_READY。
 */
aStatus_t aDrvUsartAsyncTxGetRemaining(aDrvUsartHandle_t *handle,
                                       size_t *remaining);

/**
 * @brief 停止 TX DMA 并释放其通道资源。
 * @param[in,out] handle 已初始化句柄。
 * @retval A_STATUS_OK 已停止；没有活动 DMA 时也可成功。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 不返回进度；如需要已搬运字节数，先查询 Remaining。USART 移位寄存器可能仍在发送。
 */
aStatus_t aDrvUsartAsyncTxAbort(aDrvUsartHandle_t *handle);

/**
 * @brief 查询当前实例是否具有固定 RX DMA 路由。
 * @param[in] id USART 实例 ID；查询无需初始化，不访问硬件。
 * @return 有固定路由为 A_TRUE，无路由或 ID 无效为 A_FALSE。
 * @note 不保证共享通道当前空闲。
 */
aBool_t aDrvUsartDmaRxIsSupported(aDrvUsartId_t id);

/**
 * @brief 启动不带完成回调的有限长度 DMA RX。
 * @param[in,out] handle 已初始化句柄。
 * @param[out] buffer DMA 目标，直到 Stop/Abort 前保持有效且不与其他写入者共享。
 * @param[in] size 1..65535 字节。
 * @retval A_STATUS_OK 已启动；需要查询进度并 Stop 结束本次接收。
 * @retval A_STATUS_BUSY RXNE 中断、活动 RX 或共享 DMA 资源占用。
 * @retval A_STATUS_UNSUPPORTED 实例无 RX DMA 路由。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @return 也可能返回 DMA 初始化错误。
 */
aStatus_t aDrvUsartAsyncRxStart(aDrvUsartHandle_t *handle,
                                void *buffer, size_t size);

/**
 * @brief 启动带 DMA 完成/错误 IRQ 通知的单次接收。
 * @param[in,out] handle 已初始化句柄。
 * @param[out] buffer DMA 目标，至少 size 字节，保持有效至 Stop/Abort。
 * @param[in] size 1..65535 字节。
 * @param[in] callback 非空硬件 ISR 回调，不可阻塞。
 * @param[in] argument 原样传入回调，可为 NULL；保持有效至停止并退出在途回调。
 * @return aDrvUsartAsyncRxStart() 的状态或参数错误。
 * @note 使用初始化时绑定的优先级；通知后上层仍需查询结果并 Stop。
 */
aStatus_t aDrvUsartRxDmaStart(
    aDrvUsartHandle_t *handle, void *buffer, size_t size,
    aDrvUsartDmaCallback_t callback, void *argument);

/**
 * @brief 启动持续覆盖同一缓冲区的循环 RX DMA。
 * @param[in,out] handle 已初始化句柄。
 * @param[out] buffer DMA 环形区，持续有效直到 Stop/Abort。
 * @param[in] size 环形区字节数，2..65535。
 * @param[in] callback 非空 ISR 通知回调，用于进度/错误处理，不可阻塞。
 * @param[in] argument 回调参数，可为 NULL。
 * @retval A_STATUS_OK 已启动。
 * @retval A_STATUS_BUSY RX 通道、RXNE 中断或共享 DMA 资源占用。
 * @retval A_STATUS_UNSUPPORTED 实例无 RX DMA 路由。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @return 也可能返回 DMA 配置错误。
 * @note 使用初始化时绑定的 USART/DMA 共用中断优先级。
 * @warning DMA 不等待消费者；读取时须验证覆盖，IRQ 服务延迟必须小于一整圈时间。
 */
aStatus_t aDrvUsartAsyncRxCircularStart(aDrvUsartHandle_t *handle,
                                        void *buffer, size_t size,
                                        aDrvUsartDmaCallback_t callback,
                                        void *argument);

/**
 * @brief 查询循环 DMA 自启动以来的累计字节数。
 * @param[in,out] handle 正在循环 RX 的句柄。
 * @param[out] received 累计计数，允许 size_t 自然回绕；通过无符号差值计算增量。
 * @retval A_STATUS_OK 得到一致快照。
 * @retval A_STATUS_BUSY 多次采样仍跨重装，输出不变，应重试。
 * @retval A_STATUS_ERROR DMA 错误锁存。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @warning 多圈未服务会丢失圈数，计数不能弥补无限 IRQ 延迟。
 */
aStatus_t aDrvUsartAsyncRxGetReceivedCount(aDrvUsartHandle_t *handle,
                                           size_t *received);

/** 循环 DMA 的一致快照；累计计数回绕后不能用 received % 容量求位置。 */
typedef struct {
    size_t received; /**< 累计字节数，允许无符号回绕。 */
    size_t position; /**< DMA 下一次写入的物理下标。 */
} aDrvUsartRxProgress_t;
aStatus_t aDrvUsartAsyncRxGetProgress(aDrvUsartHandle_t *handle,
                                      aDrvUsartRxProgress_t *progress);

/**
 * @brief 查询单次 RX DMA 的剩余字节。
 * @param[in,out] handle 正在单次 RX 的句柄，循环模式不适用。
 * @param[out] remaining 剩余字节数。
 * @retval A_STATUS_OK 查询成功。
 * @retval A_STATUS_ERROR DMA 错误锁存。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 没有活动单次 RX 也返回 NOT_READY。
 */
aStatus_t aDrvUsartAsyncRxGetRemaining(aDrvUsartHandle_t *handle,
                                       size_t *remaining);

/**
 * @brief 停止 RX DMA 并取回接收计数，保留已配置 DMA 资源。
 * @param[in,out] handle 正在 RX DMA 的句柄。
 * @param[out] received 可选输出；单次为已收字节，循环为累计字节，非当前有效缓存长度。
 * @retval A_STATUS_OK 已停止访问 buffer。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 无活动 RX 返回 NOT_READY；需要释放 DMA 通道时继续 Abort。
 */
aStatus_t aDrvUsartAsyncRxStop(aDrvUsartHandle_t *handle,
                               size_t *received);

/**
 * @brief 停止 RX DMA、释放通道并清理回调/错误状态。
 * @param[in,out] handle 已初始化句柄。
 * @retval A_STATUS_OK 已终止，没有活动 RX 也可成功。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 不回报长度；先 Stop 可获取计数。
 */
aStatus_t aDrvUsartAsyncRxAbort(aDrvUsartHandle_t *handle);
#endif

/**
 * @brief 更新 USART 波特率（bit/s，必须非零）。
 * @param[in,out] handle 已初始化且无中断/DMA 所有者的句柄。
 * @param[in] baud_rate 新波特率（bit/s，必须非零）。
 * @retval A_STATUS_OK 已更新硬件和句柄缓存。
 * @retval A_STATUS_BUSY 已被中断/DMA 路径占用。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @warning 调用者先保证线路空闲；此接口不等待 TC。
 */
aStatus_t aDrvUsartSetBaudrate(aDrvUsartHandle_t *handle, uint32_t baud_rate);

/**
 * @brief 读取句柄缓存中的波特率（bit/s，必须非零）。
 * @param[in] handle USART 句柄，不读取硬件寄存器。
 * @param[out] baud_rate 输出配置值；任一指针为 NULL 时不操作。
 * @note 不验证 initialized；只对成功初始化且未销毁的句柄使用。
 */
void aDrvUsartGetBaudrate(const aDrvUsartHandle_t *handle,
                          uint32_t *baud_rate);

/**
 * @brief 更新 USART 停止位枚举。
 * @param[in,out] handle 已初始化且无中断/DMA 所有者的句柄。
 * @param[in] stop_bits 新停止位枚举。
 * @retval A_STATUS_OK 已更新硬件和句柄缓存。
 * @retval A_STATUS_BUSY 已被中断/DMA 路径占用。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @warning 调用者先保证线路空闲；此接口不等待 TC。
 */
aStatus_t aDrvUsartSetStopbits(aDrvUsartHandle_t *handle,
                               aDrvUsartStopBits_t stop_bits);

/**
 * @brief 读取句柄缓存中的停止位枚举。
 * @param[in] handle USART 句柄，不读取硬件寄存器。
 * @param[out] stop_bits 输出配置值；任一指针为 NULL 时不操作。
 * @note 不验证 initialized；只对成功初始化且未销毁的句柄使用。
 */
void aDrvUsartGetStopbits(const aDrvUsartHandle_t *handle,
                          aDrvUsartStopBits_t *stop_bits);

/**
 * @brief 更新 USART 奇偶校验枚举。
 * @param[in,out] handle 已初始化且无中断/DMA 所有者的句柄。
 * @param[in] parity 新奇偶校验枚举。
 * @retval A_STATUS_OK 已更新硬件和句柄缓存。
 * @retval A_STATUS_BUSY 已被中断/DMA 路径占用。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @warning 调用者先保证线路空闲；此接口不等待 TC。
 */
aStatus_t aDrvUsartSetParity(aDrvUsartHandle_t *handle,
                             aDrvUsartParity_t parity);

/**
 * @brief 读取句柄缓存中的奇偶校验枚举。
 * @param[in] handle USART 句柄，不读取硬件寄存器。
 * @param[out] parity 输出配置值；任一指针为 NULL 时不操作。
 * @note 不验证 initialized；只对成功初始化且未销毁的句柄使用。
 */
void aDrvUsartGetParity(const aDrvUsartHandle_t *handle,
                        aDrvUsartParity_t *parity);

#endif
