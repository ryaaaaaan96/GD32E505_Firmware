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
#include <stdatomic.h>

/** @brief TX 方向当前所有权；应用不得直接修改。 */
typedef enum {
    ADEV_USART_TX_IDLE,
    ADEV_USART_TX_STREAM,
    ADEV_USART_TX_DIRECT,
    ADEV_USART_TX_ASYNC,
    ADEV_USART_TX_CALLBACK,
    ADEV_USART_TX_DRAINING,
} aDevUsartTxState_t;

/** @brief RX 方向当前所有权；应用不得直接修改。 */
typedef enum {
    ADEV_USART_RX_IDLE,
    ADEV_USART_RX_STREAM,
    ADEV_USART_RX_DIRECT,
    ADEV_USART_RX_ASYNC,
} aDevUsartRxState_t;

/** @brief 初始化时复制的设备配置；就绪后不修改，不借用调用栈。 */
typedef struct {
    aDevUsartRS485Config_t rs485;
    aDevUsartMode_t mode;
    uint8_t interrupt_priority;
    uint8_t *rx_buffer;
    size_t rx_buffer_size;
    aDevUsartRxByteCallback_t rx_byte_callback;
    void *rx_byte_context;
    uint8_t *tx_buffer;
    size_t tx_buffer_size;
} aDevUsartSettings_t;

/** @brief 发送方向运行状态，缓冲进度、等待和异步事务。 */
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
    atomic_bool completion_claimed;
    aDevUsartTxEvent_t completion_event;
    aDevUsartTxCallback_t callback;
    void *callback_argument;
    const void *async_buffer;
    size_t async_size;
    aStatus_t async_status;
    volatile aStatus_t error;
} aDevUsartTxData_t;

/** @brief 接收方向运行状态，缓冲进度、订阅和错误。 */
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
