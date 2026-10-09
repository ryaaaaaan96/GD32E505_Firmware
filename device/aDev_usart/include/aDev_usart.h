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
 * 缓冲区和
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

#ifndef ADEV_USART_STATIC_ENABLE
#define ADEV_USART_STATIC_ENABLE 0
#endif
#ifndef ADEV_USART_DYNAMIC_ENABLE
#define ADEV_USART_DYNAMIC_ENABLE 0
#endif

/**
 * @brief USART 数据路径配置字。
 *
 * TX 和 RX 各占一个互斥字段，附加功能占独立 flag。调用者必须分别选择一个
 * TX 值和一个 RX 值，并可使用按位或叠加 option。该布局与 Linux 常见的
 * “掩码字段 + 独立 flag”配置方式一致。
 */
typedef uint32_t aDevUsartMode_t;


/** @brief TX 模式字段及其有效值，三者互斥。 */
#define ADEV_USART_TX_MASK                 0x00000003U
#define ADEV_USART_TX_POLLING              0x00000000U
#define ADEV_USART_TX_INTERRUPT_BUFFERED   0x00000001U
#define ADEV_USART_TX_DMA_BUFFERED         0x00000002U

/** @brief RX 模式字段及其有效值，四者互斥。 */
#define ADEV_USART_RX_MASK                 0x0000000CU
#define ADEV_USART_RX_POLLING              0x00000000U
#define ADEV_USART_RX_INTERRUPT_BUFFERED   0x00000004U
#define ADEV_USART_RX_DMA_BUFFERED         0x00000008U
#define ADEV_USART_RX_INTERRUPT_CALLBACK   0x0000000CU

/** 字节接收钩子，限 CALLBACK 模式；在 USART ISR 中运行，不得阻塞。
 * OK 时 byte 有效，ERROR 表示硬件接收错误，byte 无效。
 * 数据由应用立即处理或复制，不再进入设备环形区。
 * context 在 DeInit 完成前有效；禁止重入设备 API、销毁或调用任务版 OS API。
 * CALLBACK 模式独占 RX，不支持 Read/ReadDirect/ReadAsync 和 IDLE 选项。 */
typedef void (*aDevUsartRxByteCallback_t)(void *context, uint8_t byte,
                                         aStatus_t status);

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

/** @brief RS485 发送方向控制方式。 */
typedef enum {
    ADEV_USART_RS485_NONE = 0, /**< 不控制 DE，包括外部自动换向电路。 */
    ADEV_USART_RS485_GPIO_DE, /**< 驱动软件控制 GPIO，TC 后释放 DE。 */
    ADEV_USART_RS485_UART_DE, /**< USART 外设自动 DE；当前后端不支持。 */
} aDevUsartRS485Mode_t;

/** @brief 方向控制配置，默认 NONE。 */
typedef struct {
    aDevUsartRS485Mode_t mode;
    /** GPIO_DE 时必填，不能与 USART TX/RX 重叠；NONE 时忽略。 */
    aDrvGpioPin_t de_pin;
    aDrvGpioLevel_t de_active_level; /**< 发送使能的物理电平。 */
} aDevUsartRS485Config_t;

/**
 * @brief USART 设备初始化配置。
 *
 * 先调用 aDevUsartConfigStructInit()，再设置硬件、TX/RX 模式及缓冲区。
 * 配置结构仅初始化期间读取；其中的缓冲区必须持续有效到 DeInit 完成。
 * TX/RX 缓冲区必须互不重叠，运行期间不得被其他设备或业务直接改写。
 * DMA 缓冲区必须处于硬件可访问的内存；当前接口不自动执行 cache 一致性维护。
 * RS485 默认 NONE；GPIO_DE 需要 TC 中断能力，即使 TX 为轮询模式。
 * UART_DE 当前返回 UNSUPPORTED；不提供方向延迟或协议帧间隔配置。
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

    aDevUsartRxByteCallback_t rx_byte_callback;
    void *rx_byte_context; /**< 回调上下文，保持有效至 DeInit。 */

} aDevUsartConfig_t;



/** @brief 可选的 USART 设备能力。 */
typedef enum {
    ADEV_USART_CAP_TX_DIRECT, /**< 当前构建与初始化后端是否支持用户缓冲区直传。 */
    ADEV_USART_CAP_RX_DIRECT, /**< 当前构建与初始化后端是否支持用户缓冲区直收。 */
} aDevUsartCapability_t;

/** @brief 不透明设备句柄，只能通过 InitStatic/Create 获取。 */
typedef struct aDevUsartHandle aDevUsartHandle_t;

/** @brief 异步 TX 终态快照；event 本身只在完成回调期间有效。 */
typedef struct {
    const void *buffer; /**< 原请求 DMA 源，回调后可由调用者回收。 */
    size_t requested; /**< 原请求字节数。 */
    size_t transferred; /**< DMA 已搬运字节数；失败时不等同于线上完整发出字节数。 */
    aStatus_t status; /**< OK 表示 TC 完成；否则为超时、取消或硬件错误。 */
} aDevUsartTxEvent_t;

/**
 * @brief 单次 TX 终态回调，统一在 USART ISR 中执行，不得阻塞。
 * @param[in] handle 原请求所属设备，回调期间不能销毁。
 * @param[in] event 本次终态快照，不得保留 event 指针。
 * @param[in] argument 原请求参数，模块不拥有其内存。
 */
typedef void (*aDevUsartTxCallback_t)(aDevUsartHandle_t *handle,
                                      const aDevUsartTxEvent_t *event,
                                      void *argument);

/** @brief 持续 RX 订阅的数据及终止事件。 */
typedef enum {
    ADEV_USART_RX_EVENT_DATA_READY, /**< 交付新数据，订阅继续。 */
    ADEV_USART_RX_EVENT_CANCELLED, /**< 订阅被显式取消。 */
    ADEV_USART_RX_EVENT_ERROR, /**< 接收/覆盖检测异常，订阅终止。 */
} aDevUsartRxEventType_t;

/** @brief RX 事件；数据在当前回调期间稳定，回调返回后不得继续持有。 */
typedef struct {
    aDevUsartRxEventType_t type; /**< 数据、超时、取消或错误。 */
    const void *buffer; /**< DATA_READY 的连续数据区间，仅回调期间借用。 */
    size_t offset;       /**< 相对 buffer 的偏移，当前为 0。 */
    size_t length;       /**< 本次连续区间长度；其他事件为 0。 */
    aStatus_t status; /**< 该终态关联的统一状态码。 */
} aDevUsartRxEvent_t;

/**
 * @brief RX 持续数据回调，统一在 USART/DMA ISR 中执行，不得阻塞。
 * @param[in] handle 原请求设备；不能在回调内销毁。
 * @param[in] event 终态及借用数据，仅当前回调有效；需要保留时自行复制。
 * @param[in] argument 原请求参数，模块不拥有其内存。
 */
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
    aDevUsartTxCallback_t callback;  /**< 必填，统一在 ISR 中执行。 */
    void *argument;                 /**< 原样传给 callback，可为 NULL。 */
} aDevUsartWriteRequest_t;


/**
 * 持续异步接收订阅；提交时复制字段，不保存此结构体指针。
 * buffer（DMA RX）和 argument 必须保持有效到 CANCELLED/ERROR 回调返回。
 */
typedef struct {
    void *buffer;                  /**< DMA RX 必填的独立快照区；IRQ RX 忽略，可为 NULL。 */
    /**< DMA RX 至少等于 rx_buffer_size；不得与 ring/其他活动缓冲区重叠。 */
    size_t buffer_size;
    aDevUsartRxCallback_t callback;  /**< 必填；通过 ReadAsync 设置，通过 Cancel 解除。 */
    void *argument;                 /**< 原样传给 callback，可为 NULL。 */
} aDevUsartReadRequest_t;

/**
 * @brief 填充 USART 配置默认值。
 *
 * 默认普通 TX/RX 和 Direct TX/RX 后端均使用轮询，中断优先级为 5，不启用 option，所有设备缓冲区
 * 为空；aDrv 子配置由 aDrvUsartConfigStructInit() 初始化。
 *
 * @param[out] config 配置结构；为 NULL 时函数不执行任何操作。
 */
void aDevUsartConfigStructInit(aDevUsartConfig_t *config);

/**
 * @brief 使用应用提供的静态存储初始化 USART。
 *
 * 通过 aDev_usart_instance.h 声明完整对象，整个设备生命周期内保持有效。
 * 对象无需预初始化；禁止对活动对象重复初始化。
 *
 * @param[in]  config 初始化配置。
 * @param[in,out] handle 调用方提供的设备对象；仅初始化成功后可使用。
 *
 * @retval A_STATUS_OK 初始化成功。
 * @retval A_STATUS_INVALID_PARAM 指针、模式、底层配置或缓冲区配置无效。
 * @retval A_STATUS_UNSUPPORTED 当前 aDrv 未提供所选中断/DMA 能力或实例映射。
 * @retval A_STATUS_BUSY 对应 USART 或所需硬件资源已经被占用。
 * @retval A_STATUS_NO_MEMORY 无法创建所需的 aOS 等待对象。
 * @retval A_STATUS_ERROR 其他底层初始化错误。
 */
#if ADEV_USART_STATIC_ENABLE
aStatus_t aDevUsartInitStatic(const aDevUsartConfig_t *config,
                              aDevUsartHandle_t *handle);
#endif

/**
 * @brief 通过 aOS 分配句柄私有状态并初始化 USART。
 * @param[in] config 初始化配置；配置本身仅调用期间读取，缓冲区持续有效至销毁。
 * @param[out] handle_out 成功返回动态句柄；参数有效时初始化失败置 NULL。
 * @return InitStatic 的状态；分配私有状态失败返回 A_STATUS_NO_MEMORY。
 * @warning 只能在任务/启动上下文调用；成功后必须使用 Destroy 释放，不能直接 Free。
 */
#if ADEV_USART_DYNAMIC_ENABLE
aStatus_t aDevUsartCreate(const aDevUsartConfig_t *config,
                          aDevUsartHandle_t **handle_out);
#endif

/**
 * @brief 停止传输并反初始化 USART 设备。
 *
 * 函数会终止已启用的 TX/RX DMA、关闭中断和底层 USART。调用期间不得有其他任务
 * 或异步订阅正在访问该 handle；活动操作或回调在途时返回 BUSY。
 * 不可从该 handle 自身的 callback 内调用。成功后静态存储可
 * 再次传给 InitStatic；动态句柄 DeInit 后仍须 Destroy 释放其存储。
 *
 * @param[in,out] handle 已初始化的设备句柄。
 * @return A_STATUS_OK 或底层返回的错误状态。
 */
aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle);

/**
 * @brief 反初始化并释放由 aDevUsartCreate() 创建的动态句柄。
 * @param[in,out] handle 动态句柄，成功返回后指针失效，调用者自行清空引用。
 * @retval A_STATUS_OK 已释放；已 DeInit 的动态句柄也可释放。
 * @retval A_STATUS_INVALID_PARAM 空句柄或由 InitStatic 创建的句柄。
 * @return 也可能返回 DeInit 错误；失败时不释放内存。
 * @warning 遵循 DeInit 的并发/回调限制，不得与任何其他访问并发。
 */
#if ADEV_USART_DYNAMIC_ENABLE
aStatus_t aDevUsartDestroy(aDevUsartHandle_t *handle);
#endif

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
 * 轮询模式直接写硬件寄存器；中断/DMA buffered 模式先复制到 TX 环形
 * 缓冲区，再由中断或 DMA 排空。非负返回值表示这些字节已被当前发送机制接收，不一定已经从 TX 引脚
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
 * 不经过 aDev RX ring；按初始化的 mode 的 RX 字段 选择 CPU 轮询直收或
 * DMA 直收。轮询也不复制到内部缓冲区，但不是“零 CPU 搬运”。函数返回后
 * 不再访问用户 buffer。等待收满或总超时到期，IDLE 不会提前结束本次读取。
 * 超时/错误前收到部分数据时返回实际长度；无数据时返回 -1 并设置 errno。
 * NO_WAIT 只取当前进度后返回，无数据返回 -1/A_EAGAIN。
 * 中断 RX ring 非空或 RX 被其他请求占用时返回 -1/A_EAGAIN，不丢弃已有数据。
 * DMA buffered RX 持续占用 RX DMA 通道，因此该模式下返回 -1/A_EAGAIN；应在
 * 初始化普通 RX 时选择 RX_POLLING 或 RX_INTERRUPT_BUFFERED 才能使用 ReadDirect。
 * 直传复用 mode 的 RX 后端；当前中断后端不提供 Direct，返回 UNSUPPORTED。
 * DMA 后端超过 65535 字节会分段，
 * 分段重装存在接收间隙，不承诺连续无丢包。size 为 0 时返回 0。
 * DMA 等待由完成/错误通知唤醒；轮询后端使用 aOS 时基和 yield。
 *
 * @param[in,out] handle 已初始化的句柄，调用期间占用 RX 路径。
 * @param[out] buffer 可写用户区域，返回前不得被其他上下文访问；DMA 时须硬件可达，零长度允许 NULL。
 * @param[in] buffer_size 字节数，不得超过 PTRDIFF_MAX。
 * @param[in] timeout 本次调用的总预算，含锁等待与分段接收。
 * @return 实际接收字节数；无进展失败返回 -1 并设置 aOS errno。
 * @warning 仅任务上下文；DIRECT_ENABLE 为 0 时不提供此声明。
 */
#if ADEV_USART_DIRECT_ENABLE
aSSize_t aDevUsartReadDirect(aDevUsartHandle_t *handle, void *buffer,
                             size_t buffer_size, aTimeout_t timeout);
#endif

/**
 * @brief 使用调用者 buffer 完成一次同步零拷贝发送。
 *
 * 按 mode 的 TX 字段 使用 CPU 轮询或 DMA，不复制到 aDev TX ring。
 * 返回后驱动/设备不再访问 data，但不表示最后一个停止位已经发出；物理排空使用
 * aDevUsartWaitTransmitComplete()。轮询后端不等于零 CPU 搬运。
 * DMA 等待使用 aOS 等待对象；TC 通知唤醒，最多每 10 ms 睡眠检查 DMA 错误。
 * 该检查不会把当前任务保持为 runnable，且不改变总 timeout 预算。
 *
 * @param[in,out] handle 已初始化的句柄，调用期间占用 TX 路径。
 * @param[in] data 可读用户区域，返回前不得修改/释放；DMA 时须硬件可达，零长度允许 NULL。
 * @param[in] data_size 字节数，不得超过 PTRDIFF_MAX。
 * @param[in] timeout 含锁等待的总预算，NO_WAIT 不保证传输全部数据。
 * @return 实际搬运字节数；无进展失败返回 -1 并设置 aOS errno；零长度返回 0。
 * @warning 仅任务上下文；DIRECT_ENABLE 为 0 时不提供此声明。
 */
#if ADEV_USART_DIRECT_ENABLE
aSSize_t aDevUsartWriteDirect(aDevUsartHandle_t *handle,
                              const void *data, size_t data_size,
                              aTimeout_t timeout);
#endif

/**
 * @brief 查询当前实例是否支持指定的可选零拷贝能力。
 * @param[in] handle USART 句柄。
 * @param[in] capability 要查询的 TX_DIRECT 或 RX_DIRECT 能力。
 * @return 编译能力与实例路由均支持时为 A_TRUE，否则 A_FALSE。
 * @note 能力可用不代表通道当前空闲；操作仍可能因模式或资源冲突失败。
 */
aBool_t aDevUsartIsSupported(const aDevUsartHandle_t *handle,
                             aDevUsartCapability_t capability);

/**
 * @brief 等待软件 TX 队列清空且 USART 硬件报告发送完成。
 *
 * 本接口用于确认最后一个停止位已经由外设发送；启用 RS485 时也确认方向已释放。
 * RS485 方向由设备自动管理，调用方不得自行修改 DE。
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
 * 调用期间不得修改或释放 buffer。单个 USART 同时只允许一笔 WriteAsync；
 * 多请求排队由应用实现，本模块不提供 TX Queue。当前仅实现 DMA 异步后端，初始化
 * mode 的 TX 字段必须选择 TX_DMA_BUFFERED 且实例有 DMA 路由。
 * 不依赖同步 Direct API 开关；一次最大 65535 字节。timeout 是从提交入口开始的总预算。
 * 提交不等待 TX 锁，竞争时立即返回 BUSY；初始化耗尽预算返回 TIMEOUT。
 * callback 统一在 USART ISR 中执行，不经过 worker；不得阻塞或重入 USART API。
 *
 * @param[in,out] handle 已初始化句柄。
 * @param[in] request 请求描述，提交时复制字段；buffer/argument 保持有效至回调结束。
 *
 * @retval A_STATUS_OK 请求已启动。
 * @retval A_STATUS_BUSY 当前 TX 被其他路径或异步请求占用。
 * @retval A_STATUS_UNSUPPORTED 当前实例没有可用 TX DMA 路由。
 * @retval A_STATUS_INVALID_PARAM 参数无效或长度超出 DMA 计数器范围。
 */
#if ADEV_USART_ASYNC_ENABLE
aStatus_t aDevUsartWriteAsync(aDevUsartHandle_t *handle,
                              const aDevUsartWriteRequest_t *request);
#endif
/**
 * @brief 停止当前异步 TX；最终结果通过原请求 callback 报告。
 * @param[in,out] handle 已初始化句柄。
 * @retval A_STATUS_OK 已受理取消；buffer/argument 保留至终态回调结束，线路可能仍在排空。
 * @retval A_STATUS_NOT_READY 未初始化或没有当前异步 TX。
 * @retval A_STATUS_INVALID_PARAM handle 为空。
 * @return 也可能返回 TX mutex 获取错误。
 * @warning 仅任务上下文；取消挂起 USART IRQ，回调可能早于返回或稍后执行，不得重入 USART API。
 */
#if ADEV_USART_ASYNC_ENABLE
aStatus_t aDevUsartWriteAsyncCancel(aDevUsartHandle_t *handle);
#endif

/**
 * @brief 启用持续异步接收；IRQ RX 借用 ring，DMA RX 复制到调用者快照区。
 * @param[in,out] handle 已初始化的中断/DMA 缓冲接收实例。
 * @param[in] request 回调、快照区及参数，提交时复制字段；存储有效到 CANCELLED/ERROR 回调退出。
 * @retval A_STATUS_OK 已订阅；回调可能早于返回执行。
 * @retval A_STATUS_BUSY RX 已占用、回调在途或 ring 中仍有未读取数据。
 * @retval A_STATUS_UNSUPPORTED 当前后端不支持异步接收。
 * @retval A_STATUS_INVALID_PARAM 参数或回调为空，或 DMA 快照区为空、过小、与 RX ring 重叠。
 * @retval A_STATUS_NOT_READY 未初始化。
 * @note 任务上下文调用。活动期间 Read/ReadDirect 返回 BUSY；重复提交不替换回调。
 * 数据回调在 IRQ 来源上下文执行，允许有界解析，不得阻塞或重入 USART API。
 * IRQ RX 保留未消费区间至回调返回，零拷贝；环绕最多分两次交付。
 * DMA RX 在 ISR 中一次复制当前可用数据至 request.buffer，校验覆盖后才交付；
 * 已交付数据在回调期间稳定，回调返回后快照区可被下一次事件改写。
 * 快照区由订阅独占，应用不得并发访问或修改；需要长期持有时在回调中另行复制。
 * 环形 DMA 要求最坏中断延迟小于一圈接收时间，多圈合并的硬件标志无法恢复计数；
 * 不承诺任意延迟下无丢包。回调长度不代表协议帧。
 * 无软件超时；需要停止时由 app 调用 Cancel。提交失败不回调。
 */
#if ADEV_USART_ASYNC_ENABLE
aStatus_t aDevUsartReadAsync(
    aDevUsartHandle_t *handle, const aDevUsartReadRequest_t *request);
#endif

/**
 * @brief 取消持续 RX 订阅，不关闭底层 ring 接收。
 * @param[in,out] handle 原订阅实例。
 * @retval A_STATUS_OK 已受理取消；快照区/参数保留至 CANCELLED 回调退出。
 * @retval A_STATUS_BUSY 回调正在执行、取消待派发或其他 RX 操作占用。
 * @retval A_STATUS_NOT_READY 未初始化或没有订阅。
 * @retval A_STATUS_INVALID_PARAM handle 为空。
 * @note 任务上下文调用；取消回调在 USART ISR 中执行，可能早于返回或稍后执行。
 * 终态回调结束前不得销毁句柄或重新订阅；回调内不得重入 USART API。
 */
#if ADEV_USART_ASYNC_ENABLE
aStatus_t aDevUsartReadAsyncCancel(aDevUsartHandle_t *handle);
#endif

/**
 * @brief 获取累计 USART IDLE 事件数。
 *
 * 仅启用 ADEV_USART_OPTION_RX_IDLE 时具有业务意义。计数用于观察事件变化，
 * 不代表 RX 环形缓冲区当前字节数，并允许自然回绕。handle 为 NULL 时返回 0。
 * @param[in] handle USART 句柄。
 * @return 累计 IDLE 事件数，不是协议帧数。
 */
uint32_t aDevUsartGetIdleEventCount(const aDevUsartHandle_t *handle);

/**
 * @brief 查询接收异常锁存标志。
 *
 * RX 环形缓冲区发生数据丢失时返回 A_TRUE。中断模式丢弃新字节，DMA 模式
 * 覆盖最旧数据。handle 为 NULL 时返回 A_FALSE。
 * @param[in] handle USART 句柄。
 * @return 当前软件溢出锁存标志。
 */
aBool_t aDevUsartHasRxOverflowed(const aDevUsartHandle_t *handle);

/**
 * @brief 清除接收异常锁存标志。
 *
 * 该函数只清除软件标志，不恢复已经丢失的数据。
 * handle 为 NULL 时不执行任何操作。
 * @param[in,out] handle USART 句柄；不清除 GetRxError 返回的错误状态。
 */
void aDevUsartClearRxOverflow(aDevUsartHandle_t *handle);

/** 任务上下文清除软件接收错误锁存；不清缓冲、溢出标志或 DMA 硬件故障。
 * 丢失的数据不可恢复，协议层自行丢弃异常帧；DMA 故障需重新初始化。
 * 清除前由应用协调接收者，避免把连续出错的数据当成新帧。 */
void aDevUsartClearRxError(aDevUsartHandle_t *handle);

/**
 * @brief 查询接收路径保存的错误状态。
 * @param[in] handle 已初始化句柄。
 * @return 当前 rx_error，无错误为 A_STATUS_OK；NULL 返回 INVALID_PARAM，
 *         未初始化返回 NOT_READY。读取不清除状态，也不设置任务 errno。
 */
aStatus_t aDevUsartGetRxError(const aDevUsartHandle_t *handle);

#endif
