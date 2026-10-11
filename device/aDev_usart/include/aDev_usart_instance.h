/**
 * @file aDev_usart_instance.h
 * @brief 供设备实例创建层静态分配使用；业务代码仅包含 aDev_usart.h。
 * 字段由 aDev 管理，调用方不得修改；布局不保证跨平台或版本兼容。
 * 初始化后的对象不得复制、移动或重复初始化，生命周期持续至 DeInit 完成。
 * 静态对象仍可能分配内部 OS 资源。
 */
#ifndef ADEV_USART_INSTANCE_H
#define ADEV_USART_INSTANCE_H

#include "aDev_usart.h"
#include "aOS.h"

/** @brief TX 回调需要区分的事务状态；同步调用由应用串行化。 */
typedef enum {
    ADEV_USART_TX_IDLE,
    ADEV_USART_TX_DIRECT,
    ADEV_USART_TX_ASYNC,
    ADEV_USART_TX_CALLBACK,
    ADEV_USART_TX_DRAINING,
} aDevUsartTxState_t;

/** @brief RX 订阅状态；同步调用由应用串行化，不另设忙标志。 */
typedef enum {
    ADEV_USART_RX_IDLE,
    ADEV_USART_RX_ASYNC,
} aDevUsartRxState_t;

/** @brief 初始化时复制的设备配置；就绪后不修改，不借用调用栈。 */
typedef struct {
    aDevUsartRS485Config_t rs485;
    aDevUsartMode_t mode;
    uint8_t *rx_buffer;
    size_t rx_buffer_size;
    aDevUsartRxByteCallback_t rx_byte_callback;
    void *rx_byte_context;
    uint8_t *tx_buffer;
    size_t tx_buffer_size;
} aDevUsartSettings_t;

/** @brief 发送方向运行状态，缓冲进度、等待和异步事务。
 * 关联状态通过 aOS 任务/ISR 临界区保护；volatile 只用于平台保证的标量快照。
 * wait_object 在初始化后不变，deadline_timer 由串行化的 TX 任务路径管理。 */
typedef struct {
    volatile size_t head;
    volatile size_t tail;
    volatile size_t count;
    volatile size_t dma_active;
    volatile aDevUsartTxState_t state;
    void *wait_object;
    aBool_t draining;
    aTimepoint_t deadline;
    aOSTimer_t deadline_timer;
    aDevUsartTxEvent_t completion_event;
    aDevUsartTxCallback_t callback;
    void *callback_argument;
    const void *async_buffer;
    size_t async_size;
    aStatus_t async_status;
    volatile aStatus_t error;
} aDevUsartTxData_t;

/** @brief 接收方向运行状态，缓冲进度、订阅和错误。
 * ring 提交与订阅交接通过 aOS 保护；独立标量快照依赖平台的单次访问保证。
 * 当前 GD32 使用自然对齐的 volatile 字段；这不是 C 线程同步或数据发布屏障。
 * IDLE 计数仅由 aDrv 串行派发的回调递增，任务只读；初始化/重置由生命周期独占。
 * 无异步订阅时，DMA 消费进度由串行调用的接收任务独占。
 * wait_object 与 dma_active 在接收启动前设置，至驱动关闭前不变。
 * 快照由订阅独占至回调退出。 */
typedef struct {
    volatile size_t head;
    volatile size_t tail;
    volatile size_t count;
    volatile size_t dma_produced;
    volatile size_t dma_consumed;
    volatile aBool_t dma_active;
    volatile aDevUsartRxState_t state;
    void *wait_object;
    uint8_t *snapshot;
    aDevUsartRxCallback_t callback;
    void *callback_argument;
    volatile aBool_t dispatching;
    volatile aBool_t cancel_pending;
    volatile uint32_t idle_event_count;
    volatile aBool_t overflow;
    volatile aStatus_t error;
} aDevUsartRxData_t;

/* 强类型实例：硬件句柄、配置、收发状态分别存放，无额外堆对象。 */
struct aDevUsartHandle {
    aDrvUsartHandle_t drv_handle;
    aDevUsartSettings_t settings;
    aDevUsartTxData_t tx;
    aDevUsartRxData_t rx;
    aDrvGpioHandle_t de_gpio;
    volatile aBool_t rs485_transmitting;
    aBool_t dynamic_storage;
};

#endif
