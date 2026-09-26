/**
 * @file aDev_usart.h
 * @brief 与具体 MCU 无关的 USART 设备接口。
 * @see docs/interface_contract.md 公共类型、错误、超时及生命周期规范。
 *
 * aDevUsart 在 aDrv 的非阻塞硬件操作之上提供可独立组合的 TX/RX 数据路径和
 * 统一超时语义。中断型数据路径通过 aOS 等待对象阻塞并由 ISR 唤醒；纯轮询
 * 路径使用 aOS 单调时基和 yield。因此本文件中的 read/write/等待接口只能在
 * 任务或线程上下文调用，不能在 ISR 中调用。
 *
 * 配置中的缓冲区均由调用者提供，模块不管理这些 DMA/ring 缓冲区的内存。
 * ReadAsync 节点另含 64 字节快照。缓冲区和
 * handle 从静态初始化或动态创建成功开始，到 DeInit/Destroy 完成为止必须持续有效。
 * 已初始化的 handle 还会被中断回调引用，禁止复制、移动或在运行期间释放。
 */

#ifndef ADEV_USART_H
#define ADEV_USART_H

#include "aDrv_usart.h"
#include "aDrv_gpio.h"
#include "aLib.h"

#include <stddef.h>
#include <stdint.h>

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
    ADEV_USART_EVENT_RX_ERROR,
} aDevUsartEvent_t;

/**
 * @brief 可选业务事件回调。
 *
 * 回调由 aOS deferred-work 队列投递，在 aOS 工作任务/线程上下文调用，不在
 * USART/DMA ISR 中直接运行。回调仍应短小；耗时业务建议由回调再通知自己的任务。
 */
typedef void (*aDevUsartEventCallback_t)(aDevUsartEvent_t event,
                                         void *argument);

/** @brief TX 模式字段及其有效值，三者互斥。 */
#define ADEV_USART_TX_MASK                 0x00000003U
#define ADEV_USART_TX_POLLING              0x00000000U
#define ADEV_USART_TX_INTERRUPT_BUFFERED   0x00000001U
#define ADEV_USART_TX_DMA_BUFFERED         0x00000002U

/** @brief RX 模式字段及其有效值，三者互斥。 */
#define ADEV_USART_RX_MASK                 0x0000000CU
#define ADEV_USART_RX_POLLING              0x00000000U
#define ADEV_USART_RX_INTERRUPT_BUFFERED   0x00000004U
#define ADEV_USART_RX_DMA_BUFFERED         0x00000008U

/**
 * @brief RX 空闲线检测；与中断缓冲 RX 或 DMA buffered RX 组合。
 *
 * 不允许与轮询 RX 组合，因为 GD32 清除 IDLE 标志需要读取数据寄存器，可能
 * 消耗尚未被轮询接口读取的末字节。DMA buffered RX 使用 IDLE 作为唤醒提示。
 */
#define ADEV_USART_OPTION_RX_IDLE          0x00000010U

/** @brief 当前定义的全部模式位，用于拒绝未知配置位。 */
#define ADEV_USART_MODE_VALID_MASK          \
    (ADEV_USART_TX_MASK | ADEV_USART_RX_MASK | \
     ADEV_USART_OPTION_RX_IDLE)

/** @brief 可选 RS485 GPIO 方向配置，由应用按板级连接填写。 */
typedef struct {
    /** 默认关闭；与 TX/RX 数据模式独立。当前通过 GPIO 控制方向。 */
    aBool_t enabled;
    /** 必填 DE 引脚；由 APP 选择，不能与 USART TX/RX 或 RE 重叠。 */
    aDrvGpioPin_t de_pin;
    /** 可选接收使能引脚；DE/RE 硬件绑在一起时只填写 de_pin。 */
    aDrvGpioPin_t re_pin;
    aDrvGpioLevel_t de_active_level;
    aDrvGpioLevel_t re_active_level;
    /** 独立 RE 引脚存在时，是否在发送期间保持接收（可能收到回显）。 */
    aBool_t receive_during_tx;
} aDevUsartRS485Config_t;

/**
 * @brief USART 设备初始化配置。
 *
 * 先调用 aDevUsartConfigStructInit()，再设置硬件、TX/RX 模式及缓冲区。
 * 配置结构仅初始化期间读取；其中的缓冲区必须持续有效到 DeInit 完成。
 * RS485 默认关闭；启用时需要 TC 中断能力，即使 TX 为轮询模式。
 * 当前不提供自动 DE、方向切换延迟或协议帧间隔配置。
 */
typedef struct {
    /** aDrv USART 基础配置：逻辑实例、TX/RX 引脚、波特率、校验和停止位。 */
    aDrvUsartConfig_t drv_config;

    /**
     * TX/RX 数据路径与附加选项的组合，默认 TX/RX 均为轮询且不启用 option。
     */
    aDevUsartMode_t mode;

    /** 可选半双工方向管理；Read 不改变方向，最终 TC 自动释放 DE。 */
    aDevUsartRS485Config_t rs485;

    /**
     * USART/DMA 中断优先级，供中断、DMA 异步及 IDLE 使用；取值必须
     * 满足当前 aDrv port 的优先级范围以及所使用 RTOS 的 ISR 调用约束。
     */
    uint8_t interrupt_priority;

    /**
     * 应用提供的共享 RX 环形缓冲区。RX 中断缓冲模式由 RXNE ISR 填充；
     * RX DMA 缓冲模式由循环 DMA 填充。Read 与 ReadAsync 共用该缓冲区。
     */
    uint8_t *rx_buffer;

    /**
     * RX 环形缓冲区容量；中断/DMA 缓冲模式要求容量至少为 2 字节。
     */
    size_t rx_buffer_size;

    /**
     * TX 环形缓冲区，中断缓冲和 DMA 缓冲发送模式使用。
     * aDevUsartWrite() 只把数据复制到该缓冲区，底层再异步排空。
     */
    uint8_t *tx_buffer;

    /** TX 环形缓冲区容量；需要该缓冲区的模式要求容量至少为 2 字节。 */
    size_t tx_buffer_size;

} aDevUsartConfig_t;

/** @brief TX 方向当前所有权；应用不得直接修改。 */
typedef enum {
    ADEV_USART_TX_IDLE,
    ADEV_USART_TX_STREAM,
    ADEV_USART_TX_DIRECT,
    ADEV_USART_TX_ASYNC,
    ADEV_USART_TX_QUEUE,
} aDevUsartTxState_t;

/** @brief RX 方向当前所有权；应用不得直接修改。 */
typedef enum {
    ADEV_USART_RX_IDLE,
    ADEV_USART_RX_STREAM,
    ADEV_USART_RX_DIRECT,
} aDevUsartRxState_t;

/** @brief 可选的 USART 设备能力。 */
typedef enum {
    ADEV_USART_CAP_TX_DIRECT,
    ADEV_USART_CAP_RX_DIRECT,
} aDevUsartCapability_t;

typedef struct aDevUsartHandle aDevUsartHandle_t;

typedef struct {
    const void *buffer;
    size_t requested;
    size_t transferred;
    aStatus_t status;
} aDevUsartTxEvent_t;

typedef void (*aDevUsartTxCallback_t)(aDevUsartHandle_t *handle,
                                      const aDevUsartTxEvent_t *event,
                                      void *argument);

typedef enum {
    ADEV_USART_RX_EVENT_DATA_READY,
    ADEV_USART_RX_EVENT_TIMEOUT,
    ADEV_USART_RX_EVENT_CANCELLED,
    ADEV_USART_RX_EVENT_ERROR,
} aDevUsartRxEventType_t;

typedef struct {
    aDevUsartRxEventType_t type;
    const void *buffer; /**< Stable node snapshot, valid only during callback; NULL on errors. */
    size_t offset;       /**< Always zero for DATA_READY snapshots. */
    size_t length;       /**< Borrowed span length; valid only during the callback. */
    aStatus_t status;
} aDevUsartRxEvent_t;

typedef void (*aDevUsartRxCallback_t)(aDevUsartHandle_t *handle,
                                      const aDevUsartRxEvent_t *event,
                                      void *argument);

/**
 * 异步发送请求。提交时复制字段，不保存此结构体指针，可使用局部变量。
 * buffer 和 argument 指向的对象必须保持有效直到完成回调。
 */
typedef struct {
    const void *buffer;             /**< DMA 源；回调前不得修改。 */
    size_t size;                    /**< 1..65535 字节。 */
    aTimeout_t timeout;             /**< 正毫秒数或 FOREVER。 */
    aDevUsartTxCallback_t callback;  /**< 必填，任务上下文执行。 */
    void *argument;                 /**< 原样传给 callback，可为 NULL。 */
} aDevUsartWriteRequest_t;

typedef uint32_t aDevUsartReadToken_t;

/**
 * 异步数据等待请求；提交时复制字段，不保存此结构体指针。
 * argument 必须保持有效到 callback 返回。
 */
typedef struct {
    aTimeout_t timeout;             /**< 等待新数据的超时；允许 NO_WAIT/FOREVER。 */
    aDevUsartRxCallback_t callback;  /**< 必填，任务上下文执行。 */
    void *argument;                 /**< 原样传给 callback，可为 NULL。 */
} aDevUsartReadRequest_t;

/** Static caller-owned storage for an opaque USART handle. */
#define ADEV_USART_STATIC_STORAGE_SIZE 1024U
typedef union {
    max_align_t alignment;
    uint8_t bytes[ADEV_USART_STATIC_STORAGE_SIZE];
} aDevUsartStorage_t;

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
 * @brief 使用应用提供的静态存储初始化 USART。
 *
 * storage 必须在设备整个生命周期内保持有效。内部状态布局不对应用公开；静态
 * 存储区容量由 ADEV_USART_STATIC_STORAGE_SIZE 定义。
 *
 * @param[in]  config 初始化配置。
 * @param[in,out] storage 调用方静态分配的存储区。
 * @param[out] handle_out 成功时返回不透明句柄。
 *
 * @retval A_STATUS_OK 初始化成功。
 * @retval A_STATUS_INVALID_PARAM 指针、模式、底层配置或缓冲区配置无效。
 * @retval A_STATUS_UNSUPPORTED 当前 aDrv 未提供所选中断/DMA 能力或实例映射。
 * @retval A_STATUS_BUSY 对应 USART 或所需硬件资源已经被占用。
 * @retval A_STATUS_NO_MEMORY 无法创建所需的 aOS 等待对象。
 * @retval A_STATUS_ERROR 其他底层初始化错误。
 */
aStatus_t aDevUsartInitStatic(const aDevUsartConfig_t *config,
                              aDevUsartStorage_t *storage,
                              aDevUsartHandle_t **handle_out);

/** @brief 通过 aOS 分配句柄私有状态并初始化 USART。 */
aStatus_t aDevUsartCreate(const aDevUsartConfig_t *config,
                          aDevUsartHandle_t **handle_out);

/**
 * @brief 停止传输并反初始化 USART 设备。
 *
 * 函数会终止已启用的 TX/RX DMA、关闭中断和底层 USART。调用期间不得有其他任务
 * 或异步请求正在访问该 handle。函数还会注销业务事件 callback 并等待已排队/执行中
 * 的 callback 退出，因此不可从该 handle 自身的 callback 内调用。成功后静态存储可
 * 再次传给 InitStatic；动态句柄可再次 Create 前必须 Destroy。
 *
 * @param[in,out] handle 已初始化的设备句柄。
 * @return A_STATUS_OK 或底层返回的错误状态。
 */
aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle);

/** @brief 反初始化并释放由 aDevUsartCreate() 创建的动态句柄。 */
aStatus_t aDevUsartDestroy(aDevUsartHandle_t *handle);

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
 * @warning 只能在任务或线程上下文调用。事件会合并为待处理位，因此回调表示
 *          “状态可能变化”，不保证每次硬件边沿分别对应一次回调；接收数据请调用
 *          Read 查询，不能把事件次数当作字节数。
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
 * @warning 只能在任务或线程上下文调用。注销不会中断已经开始执行的回调；
 *          调用方仍需保证 callback argument 在在途回调退出前有效。
 */
aStatus_t aDevUsartUnregisterEventCallback(
    aDevUsartHandle_t *handle);

/**
 * @brief 从 USART 读取最多 buffer_size 字节。
 *
 * 使用所选 RX 数据路径：轮询读取硬件、中断模式读取软件 ring、DMA buffered
 * 模式读取初始化配置的共享 DMA ring。无数据时等待首个字节；
 * 收到数据后读取当前可用内容，最多 buffer_size 字节，不为凑满长度继续等待。
 * 已读取部分数据时优先返回长度；无数据且失败时返回 -1 并设置 errno。
 *
 * 超时语义：
 * - A_TIMEOUT_NO_WAIT：检查一次，无数据时返回 -1/A_EAGAIN；
 * - A_TIMEOUT_MS(n)：在总预算内等待，到期时返回 -1/A_ETIMEDOUT；
 * - A_TIMEOUT_FOREVER：等待首个字节或底层错误，不要求收满请求长度。
 *
 * @param[in,out] handle 已初始化的设备句柄。
 * @param[out] buffer 接收目标；buffer_size 为 0 时允许为 NULL。
 * @param[in] buffer_size 请求读取的字节数，不得超过 PTRDIFF_MAX。
 * @param[in] timeout 本次完整调用共享的总等待预算。
 *
 * @return 正数表示实际读取长度，0 表示请求长度为 0，-1 表示未读取到任何数据
 *         且发生错误；返回 -1 时使用 aOSGetErrno() 查询详细原因。
 *
 * @warning 不是 ISR 安全接口；模块内部会用 RX mutex 串行化多读取者。
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
 * @warning 不是 ISR 安全接口；模块内部会用 TX mutex 保证一次 Write 的数据
 *          不会与另一个写入者按字节交错。
 */
aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *data,
                        size_t data_size, aTimeout_t timeout);

/**
 * @brief 使用调用者 buffer 完成一次同步零拷贝接收。
 *
 * 成功启动后，底层硬件直接写入 buffer，不经过 aDev RX ring。函数返回前一定
 * 停止硬件对 buffer 的访问；不支持 RX DMA/零拷贝通道的实例返回
 * -1/A_ENOTSUP。等待收满或总超时到期，IDLE 不会提前结束本次读取。
 * 超时/错误前收到部分数据时返回实际长度；无数据时返回 -1 并设置 errno。
 * NO_WAIT 仅启动后检查当前进度并停止，无数据返回 -1/A_EAGAIN。
 * 中断 RX ring 非空或 RX 被其他请求占用时返回 -1/A_EAGAIN，不丢弃已有数据。
 * DMA buffered RX 持续占用 RX DMA 通道，因此该模式下返回 -1/A_EAGAIN；应在
 * 初始化时选择 RX_POLLING 或 RX_INTERRUPT_BUFFERED 才能使用 ReadDirect。
 * 中断模式暂时屏蔽 RXNE，结束后恢复；超过 65535 字节会分段 DMA，
 * 分段重装存在接收间隙，不承诺连续无丢包。size 为 0 时返回 0。
 */
#if ADEV_USART_HAS_DMA
aSSize_t aDevUsartReadDirect(aDevUsartHandle_t *handle, void *buffer,
                             size_t buffer_size, aTimeout_t timeout);
#endif

/**
 * @brief 使用调用者 buffer 完成一次同步零拷贝发送。
 *
 * 底层硬件直接读取 data，不复制到 aDev TX ring。函数返回表示硬件不再访问
 * data，但不表示最后一个停止位已经发出；物理排空使用
 * aDevUsartWaitTransmitComplete()。不支持 TX DMA/零拷贝通道的实例返回
 * -1/A_ENOTSUP。
 */
#if ADEV_USART_HAS_DMA
aSSize_t aDevUsartWriteDirect(aDevUsartHandle_t *handle,
                              const void *data, size_t data_size,
                              aTimeout_t timeout);
#endif

/** @brief 查询当前实例是否支持指定的可选零拷贝能力。 */
aBool_t aDevUsartIsSupported(const aDevUsartHandle_t *handle,
                             aDevUsartCapability_t capability);

/**
 * @brief 等待软件 TX 队列清空且 USART 硬件报告发送完成。
 *
 * 本接口用于确认最后一个停止位已经由外设发送；启用 RS485 时也确认方向已释放。
 * RS485 方向由设备自动管理，调用方不得自行修改 DE/RE。
 * 适合需要确认线路排空等
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
 * @brief 提交一笔零拷贝异步发送。
 *
 * 成功后 buffer 由 aDev/DMA 持有，直到 callback 收到完成、超时或取消事件；
 * 调用期间不得修改或释放 buffer。单个 USART 同时只允许一笔 WriteAsync，队列
 * 模块会在此接口之上提供多请求 FIFO。TX 必须有可用 DMA 路由，不依赖普通 TX
 * mode；一次请求最大 65535 字节。timeout 从成功提交时开始计时。
 * callback 始终由 aOS deferred-work 在任务/线程上下文调用，不在 DMA/USART ISR。
 *
 * @retval A_STATUS_OK 请求已启动。
 * @retval A_STATUS_BUSY 当前 TX 被其他路径或异步请求占用。
 * @retval A_STATUS_UNSUPPORTED 当前实例没有可用 TX DMA 路由。
 * @retval A_STATUS_INVALID_PARAM 参数无效或长度超出 DMA 计数器范围。
 */
#if ADEV_USART_HAS_ASYNC
aStatus_t aDevUsartWriteAsync(aDevUsartHandle_t *handle,
                              const aDevUsartWriteRequest_t *request);
#endif
/** 停止当前异步 TX；最终结果通过原请求 callback 报告。 */
#if ADEV_USART_HAS_ASYNC
aStatus_t aDevUsartWriteAsyncCancel(aDevUsartHandle_t *handle);
#endif

/**
 * @brief 注册一次异步 RX 等待；接收数据仍存放于初始化提供的共享 RX ring。
 *
 * 仅支持 ADEV_USART_RX_DMA_BUFFERED。多个任务可以同时提交，aDev 按 FIFO
 * 顺序为各请求分配数据区间。Read 与 ReadAsync 共用一个消费游标，因此同一
 * 字节只会被一个调用者取得。DATA_READY 的 buffer 指向节点内最多 64 字节的快照，
 * offset 固定为 0；数据在 callback 返回前保持稳定，返回后不得保留指针。
 * 快照复制前后校验 DMA 进度；发生覆盖则回报 ERROR，不交付可疑字节。
 * 回调应短小且不得阻塞。DMA/USART ISR 仅提交工作项，callback 在 aOS 工作任务上下文
 * 执行。每个请求只通知一次；需要继续接收时重新提交。回调返回值不控制生命周期，
 * 使用返回的 token 显式取消。NO_WAIT 时若已有数据则立即通知，否则以 TIMEOUT
 * 事件完成。有限 timeout 从请求提交时开始计时。请求节点由 aOS 动态分配，内存
 * 不足时返回 A_STATUS_NO_MEMORY。提交失败不调用回调。不得在本 handle 的
 * ReadAsync callback 中调用 DeInit/Destroy。
 */
#if ADEV_USART_HAS_ASYNC
aStatus_t aDevUsartReadAsync(
    aDevUsartHandle_t *handle, const aDevUsartReadRequest_t *request,
    aDevUsartReadToken_t *token_out);
#endif

/**
 * @brief 取消指定的异步 RX 等待；取消结果通过请求 callback 报告。
 * 只能在任务/线程上下文调用；已经进入完成队列的请求返回 BUSY。
 */
#if ADEV_USART_HAS_ASYNC
aStatus_t aDevUsartReadAsyncCancel(aDevUsartHandle_t *handle,
                                   aDevUsartReadToken_t token);
#endif

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
 * RX 环形缓冲区发生数据丢失时返回 A_TRUE。中断模式丢弃新字节，DMA 模式
 * 覆盖最旧数据。handle 为 NULL 时返回 A_FALSE。
 */
aBool_t aDevUsartHasRxOverflowed(const aDevUsartHandle_t *handle);

/**
 * @brief 清除接收异常锁存标志。
 *
 * 该函数只清除软件标志，不恢复已经丢失的数据。
 * handle 为 NULL 时不执行任何操作。
 */
void aDevUsartClearRxOverflow(aDevUsartHandle_t *handle);
aStatus_t aDevUsartGetRxError(const aDevUsartHandle_t *handle);

#endif
