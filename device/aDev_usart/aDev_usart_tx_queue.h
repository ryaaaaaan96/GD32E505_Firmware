#ifndef ADEV_USART_TX_QUEUE_H
#define ADEV_USART_TX_QUEUE_H

#include "aDev_usart.h"
#include "aFifo.h"

#if ADEV_USART_HAS_ASYNC

typedef struct {
    /** 由用户保持有效，直到 request callback 返回。 */
    const void *buffer;
    size_t length;
    aTimeout_t timeout;
    uint32_t submitted_at_ms;
    uint32_t id;
} aDevUsartTxRequest_t;

typedef struct aDevUsartTxQueue aDevUsartTxQueueHandle_t;

/** 提交描述在调用期间复制；buffer 保持有效直到队列完成回调。
 * 所有业务回调均在 aOS worker 中执行（包含启动失败、排队超时与取消）。
 * 回调不得阻塞、等待本队列排空或销毁本队列；可提交新请求。
 * 生命周期 Init/DeInit 由一个所有者管理；DeInit 不得与新 API 调用并发。
 * Init 为一个工作项分配内存，失败返回 NO_MEMORY；描述符与 payload 仍由调用者提供。
 */
typedef struct {
    const void *buffer;
    size_t size;
    aTimeout_t timeout;
} aDevUsartTxQueueRequest_t;

typedef struct {
    /** 必须配置为支持 TX DMA 的 aDevUsart 实例。 */
    aDevUsartHandle_t *usart;
    /** 调用者提供的固定容量 FIFO 元数据数组。 */
    aDevUsartTxRequest_t *request_storage;
    size_t request_capacity;
    void (*callback)(aDevUsartTxQueueHandle_t *queue,
                     uint32_t request_id,
                     const aDevUsartTxEvent_t *event,
                     void *argument);
    void *argument;
} aDevUsartTxQueueConfig_t;

struct aDevUsartTxQueue {
    aDevUsartHandle_t *usart;
    aFifo_t fifo;
    void *work;
    aDevUsartTxEvent_t completion;
    aBool_t completion_ready;
    aBool_t callback_active;
    aBool_t closing;
    uint32_t next_id;
    void *mutex;
    void *drained;
    void (*callback)(aDevUsartTxQueueHandle_t *queue,
                     uint32_t request_id,
                     const aDevUsartTxEvent_t *event,
                     void *argument);
    void *argument;
    aBool_t initialized;
    aBool_t active;
    aBool_t cancelling;
};

/** 填充队列配置默认值。 */
void aDevUsartTxQueueConfigStructInit(aDevUsartTxQueueConfig_t *config);
/** 初始化队列并独占该 USART 的 TX；生命周期内不得复制 queue。 */
aStatus_t aDevUsartTxQueueInit(const aDevUsartTxQueueConfig_t *config,
                            aDevUsartTxQueueHandle_t *queue);
/** 按 FIFO 提交零拷贝 TX；成功后 buffer 保持有效到对应 callback。 */
aStatus_t aDevUsartTxQueueSubmit(aDevUsartTxQueueHandle_t *queue,
                              const aDevUsartTxQueueRequest_t *submission,
                              uint32_t *request_id);
/** 取消活动请求及所有等待请求；每个请求仍各收到一次终结 callback。 */
aStatus_t aDevUsartTxQueueCancelAll(aDevUsartTxQueueHandle_t *queue);
aStatus_t aDevUsartTxQueueWaitDrained(aDevUsartTxQueueHandle_t *queue,
                                   aTimeout_t timeout);
size_t aDevUsartTxQueueGetPendingCount(aDevUsartTxQueueHandle_t *queue);
/** 所有请求及其回调均已完成时返回 A_TRUE。 */
aBool_t aDevUsartTxQueueIsIdle(aDevUsartTxQueueHandle_t *queue);
aStatus_t aDevUsartTxQueueDeInit(aDevUsartTxQueueHandle_t *queue);

#endif /* ADEV_USART_HAS_ASYNC */
#endif
