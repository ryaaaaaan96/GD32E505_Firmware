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

struct aDevUsartHandle {
    aDrvUsartHandle_t drv_handle;
    aDevUsartRS485Config_t rs485;
    aDrvGpioHandle_t de_gpio;
    volatile aBool_t rs485_transmitting;
    aDevUsartMode_t mode;
    uint8_t interrupt_priority;
    uint8_t *rx_buffer;
    size_t rx_buffer_size;
    aDevUsartRxByteCallback_t rx_byte_callback;
    void *rx_byte_context;
    volatile size_t rx_head;
    volatile size_t rx_tail;
    volatile size_t rx_count;
    volatile size_t rx_dma_produced;
    volatile size_t rx_dma_consumed;
    volatile aBool_t rx_dma_active;
    uint8_t *tx_buffer;
    size_t tx_buffer_size;
    volatile size_t tx_head;
    volatile size_t tx_tail;
    volatile size_t tx_count;
    volatile size_t tx_dma_active;
    volatile aDevUsartTxState_t tx_state;
    volatile aDevUsartRxState_t rx_state;
    void *rx_mutex;
    void *tx_mutex;
    void *rx_wait_object;
    void *tx_wait_object;
    aBool_t tx_draining;
    aTimepoint_t tx_deadline;
    aOSTimer_t tx_deadline_timer;
    atomic_bool tx_completion_claimed;
    aDevUsartTxEvent_t tx_completion_event;
    aDevUsartTxCallback_t tx_callback;
    void *tx_callback_argument;
    const void *tx_async_buffer;
    size_t tx_async_size;
    aStatus_t tx_async_status;
    uint8_t *rx_snapshot;
    aDevUsartRxCallback_t rx_callback;
    void *rx_callback_argument;
    volatile aBool_t rx_dispatching;
    volatile aBool_t rx_cancel_pending;
    volatile uint32_t idle_event_count;
    volatile aBool_t rx_overflow;
    volatile aStatus_t rx_error;
    volatile aStatus_t tx_error;
    aBool_t dynamic_storage;
};

#endif
