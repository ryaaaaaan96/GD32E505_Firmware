/**
 * @file aDev_usart.h
 * @brief 与具体 MCU 无关的 USART 设备接口。
 *
 * aDevUsart 在 aDrv 的非阻塞硬件操作之上提供可独立组合的 TX/RX 数据路径和
 * 统一超时语义。中断型数据路径通过 aOS 等待对象阻塞并由 ISR 唤醒；纯轮询
 * 路径使用 aOS 单调时基和 yield。因此本文件中的 read/write/等待接口只能在
 * 任务或线程上下文调用，不能在 ISR 中调用。
 *
 * 配置中的所有缓冲区都由调用者提供，模块不会申请或释放缓冲区内存。缓冲区和
 * handle 从 aDevUsartInit() 成功开始到 aDevUsartDeInit() 完成为止必须持续有效。
 * 已初始化的 handle 还会被中断回调引用，禁止复制、移动或在运行期间释放。
 */

#ifndef ADEV_USART_H
#define ADEV_USART_H

#include "aDrv_usart.h"
#include "aLib.h"

/**
 * @brief USART 数据路径配置字。
 *
 * TX 和 RX 各占一个互斥字段，附加功能占独立 flag。调用者必须分别选择一个
 * TX 值和一个 RX 值，并可使用按位或叠加 option。该布局与 Linux 常见的
 * “掩码字段 + 独立 flag”配置方式一致。
 */
typedef uint32_t aDevUsartMode_t;

/** @brief 与具体 USART 硬件标志无关的设备事件。 */
typedef enum {
    ADEV_USART_EVENT_RX_READY,
    ADEV_USART_EVENT_RX_IDLE,
    ADEV_USART_EVENT_TX_SPACE,
    ADEV_USART_EVENT_TX_COMPLETE,
} aDevUsartEvent_t;

/**
 * @brief 可选业务事件回调。
 *
 * 回调由 aDev 在底层 ISR 更新完内部状态后调用，因此运行在 ISR 上下文。回调
 * 只能执行 ISR-safe 操作，复杂业务应投递给任务处理。
 */
typedef void (*aDevUsartEventCallback_t)(aDevUsartEvent_t event,
                                         void *argument);

/** @brief TX 模式字段及其有效值，三者互斥。 */
#define ADEV_USART_TX_MASK                 0x00000003U
#define ADEV_USART_TX_POLLING              0x00000000U
#define ADEV_USART_TX_INTERRUPT_BUFFERED   0x00000001U
#define ADEV_USART_TX_DMA_DIRECT           0x00000002U

/** @brief RX 模式字段及其有效值，三者互斥。 */
#define ADEV_USART_RX_MASK                 0x0000000CU
#define ADEV_USART_RX_POLLING              0x00000000U
#define ADEV_USART_RX_INTERRUPT_BUFFERED   0x00000004U
#define ADEV_USART_RX_DMA_CIRCULAR         0x00000008U

/**
 * @brief RX 空闲线检测；可与中断缓冲 RX 或循环 DMA RX 组合。
 *
 * 不允许与轮询 RX 组合，因为 GD32 清除 IDLE 标志需要读取数据寄存器，可能
 * 消耗尚未被轮询接口读取的末字节。
 */
#define ADEV_USART_OPTION_RX_IDLE          0x00000010U

/** @brief 当前定义的全部模式位，用于拒绝未知配置位。 */
#define ADEV_USART_MODE_VALID_MASK          \
    (ADEV_USART_TX_MASK | ADEV_USART_RX_MASK | \
     ADEV_USART_OPTION_RX_IDLE)

/**
 * @brief USART 设备初始化配置。
 *
 * 使用 aDevUsartConfigStructInit() 设置默认值后，再填写实例、引脚、波特率、
 * TX/RX 模式、option 和相应缓冲区。配置结构本身只在初始化期间读取，但其中
 * 指向的缓冲区会由设备长期使用。
 */
typedef struct {
    /** aDrv USART 基础配置：逻辑实例、TX/RX 引脚、波特率、校验和停止位。 */
    aDrvUsartConfig_t drv_config;

    /**
     * TX/RX 数据路径与附加选项的组合，默认 TX/RX 均为轮询且不启用 option。
     */
    aDevUsartMode_t mode;

    /**
     * USART/DMA 中断优先级，仅中断、循环 DMA 或 IDLE 配置使用；取值必须
     * 满足当前 aDrv port 的优先级范围以及所使用 RTOS 的 ISR 调用约束。
     */
    uint8_t interrupt_priority;

    /**
     * 应用可读取的 RX 环形缓冲区。RX 中断缓冲和循环 DMA 模式必需。在循环
     * DMA 模式中，这也是 DMA 直接写入的目标缓冲区。
     */
    uint8_t *rx_buffer;

    /**
     * RX 环形缓冲区容量；需要该缓冲区的模式要求容量至少为 2 字节。
     * GD32E505 循环 DMA 模式还要求容量不超过 65535 字节。
     */
    size_t rx_buffer_size;

    /**
     * TX 环形缓冲区，中断发送模式使用。aDevUsartWrite() 先写入该缓冲区，
     * TBE ISR 再逐字节送入 USART。
     */
    uint8_t *tx_buffer;

    /** TX 环形缓冲区容量；需要该缓冲区的模式要求容量至少为 2 字节。 */
    size_t tx_buffer_size;

} aDevUsartConfig_t;

/**
 * @brief USART 设备运行句柄。
 *
 * 句柄字段由 aDevUsart 管理并被任务与 ISR 共享。应用可以静态分配句柄，但不应
 * 直接读写内部字段；状态查询应使用本文件提供的公共函数。
 */
typedef struct {
    /** 底层 aDrv USART 句柄。 */
    aDrvUsartHandle_t drv_handle;
    /** 初始化时选择的 TX/RX 数据路径和附加选项。 */
    aDevUsartMode_t mode;

    /** RX 环形缓冲区及容量。 */
    uint8_t *rx_buffer;
    size_t rx_buffer_size;

    /** aDev 已观察并提交到 RX 环形队列的 DMA 累计接收量。 */
    size_t rx_dma_observed;

    /** RX 环形缓冲区写入位置、读取位置和当前有效字节数。 */
    volatile size_t rx_head;
    volatile size_t rx_tail;
    volatile size_t rx_count;

    /** TX 环形缓冲区及容量。 */
    uint8_t *tx_buffer;
    size_t tx_buffer_size;

    /** TX 环形缓冲区写入位置、发送位置和当前待发送字节数。 */
    volatile size_t tx_head;
    volatile size_t tx_tail;
    volatile size_t tx_count;

    /** aOS 内部等待对象；保持为 void 指针以避免向公共头文件暴露 OS 类型。 */
    void *rx_wait_object;
    void *tx_wait_object;

    /** 通过 aDevUsartRegisterEventCallback() 绑定的业务事件回调。 */
    aDevUsartEventCallback_t event_callback;
    void *event_argument;

    /** IDLE 事件累计计数；允许 uint32_t 自然回绕。 */
    volatile uint32_t idle_event_count;

    /**
     * 接收异常锁存标志：RX 环形缓冲区被 DMA 追上并覆盖未读数据，或者 DMA
     * 报告传输错误时置位。
     */
    volatile aBool_t rx_overflow;
} aDevUsartHandle_t;

/**
 * @brief 填充 USART 配置默认值。
 *
 * 默认 TX/RX 均使用轮询，中断优先级为 5，不启用 option，所有设备缓冲区
 * 为空；aDrv 子配置由 aDrvUsartConfigStructInit() 初始化。
 *
 * @param[out] config 配置结构；为 NULL 时函数不执行任何操作。
 */
void aDevUsartConfigStructInit(aDevUsartConfig_t *config);

/**
 * @brief 将 USART 句柄重置为未初始化状态。
 *
 * 该函数只清零软件状态，不访问硬件。首次初始化前以及复用已反初始化句柄前均
 * 应调用本函数。不得对正在运行或仍可能被 ISR 引用的句柄调用。
 *
 * @param[out] handle 设备句柄；为 NULL 时函数不执行任何操作。
 */
void aDevUsartHandleStructInit(aDevUsartHandle_t *handle);

/**
 * @brief 初始化一个 USART 设备实例。
 *
 * 调用前必须先初始化 config 和 handle，再填写模式所需配置。初始化成功后，
 * handle 及配置中的缓冲区地址必须保持不变，直到完成反初始化。
 *
 * @param[in]  config 初始化配置。
 * @param[out] handle 设备句柄。
 *
 * @retval A_STATUS_OK 初始化成功。
 * @retval A_STATUS_INVALID_PARAM 指针、模式、底层配置或缓冲区配置无效。
 * @retval A_STATUS_UNSUPPORTED 当前 aDrv 未提供所选中断/DMA 能力或实例映射。
 * @retval A_STATUS_BUSY 对应 USART 或所需硬件资源已经被占用。
 * @retval A_STATUS_NO_MEMORY 无法创建所需的 aOS 等待对象。
 * @retval A_STATUS_ERROR 其他底层初始化错误。
 */
aStatus_t aDevUsartInit(const aDevUsartConfig_t *config,
                        aDevUsartHandle_t *handle);

/**
 * @brief 停止传输并反初始化 USART 设备。
 *
 * 函数会终止已启用的 TX/RX DMA、关闭中断和底层 USART。调用期间不得有其他
 * 任务正在访问该 handle。成功后可以重新调用 aDevUsartHandleStructInit() 并
 * 复用它。
 *
 * @param[in,out] handle 已初始化的设备句柄。
 * @return A_STATUS_OK 或底层返回的错误状态。
 */
aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle);

/**
 * @brief 注册或替换 USART 的硬件无关异步事件回调。
 *
 * 注册后，aDev 会在维护完内部缓冲区、DMA 位置和等待对象之后报告设备事件。
 * 回调只用于通知，不拥有传输缓冲区，也不替代 aDevUsartRead()、
 * aDevUsartWrite() 或 aDevUsartWaitTransmitComplete()。
 *
 * 同一 handle 同一时刻只保存一个业务回调。再次调用本函数会原子地替换原回调；
 * 不再需要通知时调用 aDevUsartUnregisterEventCallback()。
 *
 * @param[in,out] handle 已初始化的设备句柄。
 * @param[in] callback 业务事件回调，不得为 NULL。
 * @param[in] argument 调用 callback 时原样传回的用户参数，允许为 NULL。
 *
 * @retval A_STATUS_OK 注册成功。
 * @retval A_STATUS_INVALID_PARAM handle 或 callback 为空。
 * @retval A_STATUS_NOT_READY USART 尚未初始化。
 *
 * @warning 只能在任务或线程上下文调用。回调运行在 ISR 上下文，不能调用阻塞
 *          接口；应使用 ISR-safe 机制通知业务任务。
 */
aStatus_t aDevUsartRegisterEventCallback(
    aDevUsartHandle_t *handle,
    aDevUsartEventCallback_t callback,
    void *argument);

/**
 * @brief 注销 USART 的业务异步事件回调。
 *
 * 函数返回后，该 handle 的后续设备事件不再调用业务回调；aDev 内部的 ISR
 * 处理、环形缓冲区维护和任务唤醒不受影响。
 *
 * @param[in,out] handle 已初始化的设备句柄。
 *
 * @retval A_STATUS_OK 注销成功；没有已注册回调时同样返回成功。
 * @retval A_STATUS_INVALID_PARAM handle 为空。
 * @retval A_STATUS_NOT_READY USART 尚未初始化。
 *
 * @warning 只能在任务或线程上下文调用。
 */
aStatus_t aDevUsartUnregisterEventCallback(
    aDevUsartHandle_t *handle);

/**
 * @brief 从 USART 读取最多 buffer_size 字节。
 *
 * 函数会尝试取得完整请求长度。等待到期或发生错误前已经收到数据时，优先返回
 * 部分长度；只有没有任何可报告数据时才返回 -1 并通过 aOSGetErrno() 报告原因。
 * 在 DMA RX + IDLE 模式下，本函数会读取 DMA 当前写入位置，因此已经由 DMA
 * 写入环形缓冲区的数据无需等待 IDLE 事件即可对本接口可见。
 *
 * 超时语义：
 * - A_TIMEOUT_NO_WAIT：检查一次，无数据时返回 -1/A_EAGAIN；
 * - A_TIMEOUT_MS(n)：在总预算内等待，到期时返回 -1/A_ETIMEDOUT；
 * - A_TIMEOUT_FOREVER：持续等待，直到完成请求或底层返回错误。
 *
 * @param[in,out] handle 已初始化的设备句柄。
 * @param[out] buffer 接收目标；buffer_size 为 0 时允许为 NULL。
 * @param[in] buffer_size 请求读取的字节数，不得超过 PTRDIFF_MAX。
 * @param[in] timeout 本次完整调用共享的总等待预算。
 *
 * @return 正数表示实际读取长度，0 表示请求长度为 0，-1 表示未读取到任何数据
 *         且发生错误；返回 -1 时使用 aOSGetErrno() 查询详细原因。
 *
 * @warning 不是 ISR 安全接口；同一 handle 的多读取者必须由上层串行化。
 */
aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *buffer,
                       size_t buffer_size, aTimeout_t timeout);

/**
 * @brief 向 USART 提交最多 data_size 字节。
 *
 * 函数在不同模式下分别把数据写入硬件寄存器、启动 DMA，或复制到 TX 环形
 * 缓冲区。非负返回值表示这些字节已被当前发送机制接收，不一定已经从 TX 引脚
 * 完整移出；需要确认物理发送完成时继续调用
 * aDevUsartWaitTransmitComplete()。
 *
 * 等待到期或发生错误前已经提交数据时，优先返回部分长度；只有一个字节也未
 * 提交时才返回 -1 并设置 errno。超时规则与 aDevUsartRead() 相同。
 *
 * @param[in,out] handle 已初始化的设备句柄。
 * @param[in] data 发送数据；data_size 为 0 时允许为 NULL。
 * @param[in] data_size 请求发送的字节数，不得超过 PTRDIFF_MAX。
 * @param[in] timeout 本次完整调用共享的总等待预算。
 *
 * @return 正数表示实际提交长度，0 表示请求长度为 0，-1 表示未提交任何数据
 *         且发生错误；返回 -1 时使用 aOSGetErrno() 查询详细原因。
 *
 * @warning 不是 ISR 安全接口；多个写入者必须由上层互斥，否则不同消息可能
 *          在 TX 环形缓冲区中按字节交错。
 */
aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *data,
                        size_t data_size, aTimeout_t timeout);

/**
 * @brief 等待软件 TX 队列清空且 USART 硬件报告发送完成。
 *
 * 本接口用于确认最后一个停止位已经由外设发送，适合 RS485 切换收发方向等
 * 场景。它直接返回 aStatus_t，不设置 errno。
 *
 * @param[in,out] handle 已初始化的设备句柄。
 * @param[in] timeout 等待预算。
 *
 * @retval A_STATUS_OK 软件队列和硬件发送均已完成。
 * @retval A_STATUS_BUSY 使用 A_TIMEOUT_NO_WAIT 检查时仍未完成。
 * @retval A_STATUS_TIMEOUT 有限等待到期。
 * @retval A_STATUS_INVALID_PARAM handle 或 timeout 无效。
 * @return 也可能返回底层发送状态查询产生的其他错误。
 *
 * @warning 不是 ISR 安全接口。
 */
aStatus_t aDevUsartWaitTransmitComplete(aDevUsartHandle_t *handle,
                                        aTimeout_t timeout);

/**
 * @brief 获取累计 USART IDLE 事件数。
 *
 * 仅启用 ADEV_USART_OPTION_RX_IDLE 时具有业务意义。计数用于观察事件变化，
 * 不代表 RX 环形缓冲区当前字节数，并允许自然回绕。handle 为 NULL 时返回 0。
 */
uint32_t aDevUsartGetIdleEventCount(const aDevUsartHandle_t *handle);

/**
 * @brief 查询接收异常锁存标志。
 *
 * RX 环形缓冲区已满而覆盖未读数据，或者循环 DMA 报告传输错误时返回
 * A_TRUE。handle 为 NULL 时返回 A_FALSE。
 */
aBool_t aDevUsartHasRxOverflowed(const aDevUsartHandle_t *handle);

/**
 * @brief 清除接收异常锁存标志。
 *
 * 该函数只清除软件标志，不恢复已经丢失的数据，也不会重新启动失败的 DMA。
 * handle 为 NULL 时不执行任何操作。
 */
void aDevUsartClearRxOverflow(aDevUsartHandle_t *handle);

#endif
