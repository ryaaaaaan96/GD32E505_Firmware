/* Host test: real device implementation, simulated nonblocking driver and OS. */
#include "aDev_usart_internal.h"
#include "aDev_usart_tx_queue.h"
#include "aOS.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static aDrvGpioLevel_t levels[256];
static aBool_t irq_supported = A_TRUE;
static aBool_t tc = A_TRUE;
static aBool_t dma_stall;
static aBool_t dma_error;
static size_t dma_remaining;
static size_t byte_budget = 100;
static size_t rx_byte_budget = SIZE_MAX;
static size_t direct_rx_count;
static void *direct_rx_target;
static unsigned int rx_dma_stops;
static unsigned int poll_waits;
static unsigned int rx_waits;
static unsigned int mutex_count;
static unsigned int locks;
static aStatus_t last_error;
static const void *dma_source;
static aDrvUsartDmaCallback_t rx_dma_callback;
static void *rx_dma_argument;
static size_t rx_dma_size;
static size_t rx_dma_remaining;
static aBool_t rx_dma_circular;
static size_t rx_dma_produced;
static aStatus_t rx_dma_progress_status = A_STATUS_OK;
static aBool_t in_worker;
static aBool_t overwrite_in_callback;
static uint8_t *rx_ring;
static aBool_t overwrite_during_copy;
static unsigned int count_reads;
static unsigned int async_tx_callbacks;
static unsigned int async_rx_callbacks;
static aDevUsartRxEventType_t async_rx_reason;
static aStatus_t async_tx_status;
static size_t async_rx_length;
static unsigned int queue_callbacks;
static uint32_t queue_callback_ids[4];
static aStatus_t queue_callback_statuses[4];
static aOSWorkItem_t *work_items[16];
static aOSWorkFunction_t work_functions[16];
static void *work_arguments[16];
static size_t work_count;
static uint32_t uptime_ms;
typedef struct {
    aOSTimerCallback_t callback;
    void *argument;
} MockTimer;

static void drain_work(void)
{
    while (work_count != 0U) {
        aOSWorkItem_t *item = work_items[0];
        aOSWorkFunction_t function = work_functions[0];
        void *argument = work_arguments[0];
        for (size_t i = 1U; i < work_count; ++i) {
            work_items[i - 1U] = work_items[i];
            work_functions[i - 1U] = work_functions[i];
            work_arguments[i - 1U] = work_arguments[i];
        }
        --work_count;
        item->queued = A_FALSE;
        item->running = A_TRUE;
        in_worker = A_TRUE;
        function(argument);
        in_worker = A_FALSE;
        item->running = A_FALSE;
    }
}

static aStatus_t enqueue_work(aOSWorkItem_t *item,
                              aOSWorkFunction_t function, void *argument)
{
    if (item->queued) return A_STATUS_BUSY;
    assert(work_count < 16U);
    item->queued = A_TRUE;
    work_items[work_count] = item;
    work_functions[work_count] = function;
    work_arguments[work_count++] = argument;
    return A_STATUS_OK;
}

void *aOSAlloc(size_t size) { return malloc(size); }
void aOSFree(void *memory) { free(memory); }

uint32_t aOSGetUptimeMs(void) { return uptime_ms; }
aStatus_t aOSValidateIsrPriority(uint32_t priority)
{ return priority >= 5U ? A_STATUS_OK : A_STATUS_INVALID_PARAM; }
void aOSYield(void) {}
aStatus_t aOSWaitObjectCreate(void **p) { *p = p; return A_STATUS_OK; }
void aOSWaitObjectDestroy(void **p) { *p = NULL; }
aStatus_t aOSWaitObjectWait(void *p, aTimeout_t t)
{ (void)p; (void)t; ++rx_waits; return A_STATUS_TIMEOUT; }
void aOSWaitObjectNotify(void *p) { (void)p; }
void aOSWaitObjectNotifyFromISR(void *p) { (void)p; }
aStatus_t aOSMutexCreate(void **p) { ++mutex_count; *p = p; return A_STATUS_OK; }
void aOSMutexDestroy(void **p) { if (*p) --mutex_count; *p = NULL; }
aStatus_t aOSMutexLock(void *p, aTimeout_t t)
{ (void)t; assert(p); assert(locks < 4U); ++locks; return A_STATUS_OK; }
aStatus_t aOSMutexUnlock(void *p)
{ assert(p); assert(locks > 0U); --locks; return A_STATUS_OK; }
void aOSWorkItemInit(aOSWorkItem_t *item)
{ memset(item, 0, sizeof(*item)); }
aStatus_t aOSWorkSubmitFromISR(aOSWorkItem_t *item,
                               aOSWorkFunction_t function, void *argument)
{ return enqueue_work(item, function, argument); }
aStatus_t aOSWorkWaitIdle(aOSWorkItem_t *item, aTimeout_t timeout)
{ (void)item; (void)timeout; drain_work(); return A_STATUS_OK; }
aStatus_t aOSWorkSubmit(aOSWorkItem_t *item,
                        aOSWorkFunction_t function, void *argument)
{ return enqueue_work(item, function, argument); }
aStatus_t aOSTimerCreate(aOSTimer_t *timer, aOSTimerCallback_t callback,
                         void *argument)
{
    MockTimer *mock = malloc(sizeof(*mock));
    assert(mock != NULL);
    mock->callback = callback;
    mock->argument = argument;
    *timer = mock;
    return A_STATUS_OK;
}
aStatus_t aOSTimerStart(aOSTimer_t timer, uint32_t milliseconds)
{ (void)timer; (void)milliseconds; return A_STATUS_OK; }
void aOSTimerStop(aOSTimer_t timer) { (void)timer; }
void aOSTimerDestroy(aOSTimer_t *timer) { free(*timer); *timer = NULL; }
void aOSCriticalEnter(void) {}
void aOSCriticalExit(void) {}
aOSCriticalState_t aOSCriticalEnterFromISR(void) { return 0U; }
void aOSCriticalExitFromISR(aOSCriticalState_t state) { (void)state; }
aSSize_t aOSFailWithStatus(aStatus_t s) { last_error = s; return -1; }
aSSize_t aOSFailWithTimeout(aTimeout_t t)
{ (void)t; last_error = A_STATUS_TIMEOUT; return -1; }
aBool_t aOSPollWaitExpired(const aTimepoint_t *p)
{ (void)p; ++poll_waits; return A_TRUE; }

void aDrvGpioConfigStructInit(aDrvGpioConfig_t *p) { memset(p, 0, sizeof(*p)); }
void aDrvGpioHandleStructInit(aDrvGpioHandle_t *p) { memset(p, 0, sizeof(*p)); }
aStatus_t aDrvGpioInit(const aDrvGpioConfig_t *c, aDrvGpioHandle_t *h)
{ h->pin = c->pin; h->initialized = A_TRUE; levels[h->pin] = c->initial_level; return A_STATUS_OK; }
aStatus_t aDrvGpioDeInit(aDrvGpioHandle_t *h) { h->initialized = A_FALSE; return A_STATUS_OK; }
aStatus_t aDrvGpioWrite(const aDrvGpioHandle_t *h, aDrvGpioLevel_t l)
{ assert(h->initialized); levels[h->pin] = l; return A_STATUS_OK; }
void aDrvUsartConfigStructInit(aDrvUsartConfig_t *p)
{ memset(p, 0, sizeof(*p)); p->tx_pin = 9; p->rx_pin = 10; }
void aDrvUsartHandleStructInit(aDrvUsartHandle_t *p) { memset(p, 0, sizeof(*p)); }
aStatus_t aDrvUsartInitStatic(const aDrvUsartConfig_t *c, aDrvUsartHandle_t *h)
{ (void)c; h->initialized = A_TRUE; return A_STATUS_OK; }
aStatus_t aDrvUsartDeInitStatic(aDrvUsartHandle_t *h)
{ h->initialized = A_FALSE; return A_STATUS_OK; }
aBool_t aDrvUsartInterruptIsSupported(void) { return irq_supported; }
void aDrvUsartDisableInterrupt(aDrvUsartHandle_t *h) { (void)h; }
void aDrvUsartEnableInterrupt(aDrvUsartHandle_t *h) { (void)h; }
aStatus_t aDrvUsartSetInterruptEnabled(aDrvUsartHandle_t *h, aDrvUsartExti_t i, aBool_t en)
{ if(en) h->interrupt_enabled_mask |= 1U << i; else h->interrupt_enabled_mask &= ~(1U << i); return A_STATUS_OK; }
aStatus_t aDrvUsartRegisterCallback(aDrvUsartHandle_t *h, const aDrvUsartExtiConfig_t *c)
{ h->callbacks[c->trigger].function = c->callback; h->callbacks[c->trigger].argument = c->argument; return aDrvUsartSetInterruptEnabled(h,c->trigger,c->enabled); }
aStatus_t aDrvUsartTryWriteByte(aDrvUsartHandle_t *h, uint8_t b)
{ (void)h; (void)b; if (!byte_budget) return A_STATUS_BUSY; --byte_budget; tc = A_FALSE; return A_STATUS_OK; }
aStatus_t aDrvUsartTryReadByte(aDrvUsartHandle_t *h, uint8_t *b)
{ (void)h; if (!rx_byte_budget) return A_STATUS_BUSY;
  --rx_byte_budget; *b = 42; return A_STATUS_OK; }
aStatus_t aDrvUsartIsTransmitComplete(const aDrvUsartHandle_t *h, aBool_t *v)
{ (void)h; *v = tc; return A_STATUS_OK; }
aBool_t aDrvUsartAsyncTxIsSupported(const aDrvUsartHandle_t *h) { (void)h; return A_TRUE; }
aBool_t aDrvUsartAsyncRxIsSupported(const aDrvUsartHandle_t *h) { (void)h; return A_TRUE; }
aStatus_t aDrvUsartAsyncTxStart(aDrvUsartHandle_t *h, const void *p, size_t n, size_t *s)
{ (void)h; dma_source = p; dma_remaining = n; *s = n; tc = A_FALSE; return A_STATUS_OK; }
aStatus_t aDrvUsartAsyncTxGetRemaining(aDrvUsartHandle_t *h, size_t *n)
{ (void)h; *n = dma_stall ? dma_remaining : 0; return dma_error ? A_STATUS_ERROR : A_STATUS_OK; }
aStatus_t aDrvUsartAsyncTxAbort(aDrvUsartHandle_t *h)
{ (void)h; dma_remaining = 0; return A_STATUS_OK; }
aStatus_t aDrvUsartAsyncRxStart(aDrvUsartHandle_t *h, void *p, size_t n)
{ (void)h; direct_rx_target = p; rx_dma_size = n;
  size_t count = direct_rx_count < n ? direct_rx_count : n;
  memset(p, 0x5a, count); rx_dma_remaining = n - count; return A_STATUS_OK; }
aStatus_t aDrvUsartAsyncRxAbort(aDrvUsartHandle_t *h)
{ (void)h; rx_dma_circular = A_FALSE; rx_dma_callback = NULL; return A_STATUS_OK; }
aStatus_t aDrvUsartAsyncRxCircularStart(aDrvUsartHandle_t *h, void *p, size_t n,
                                         uint8_t priority,
                                         aDrvUsartDmaCallback_t callback,
                                         void *argument)
{ (void)h; (void)priority; rx_ring = p;
  rx_dma_circular = A_TRUE; rx_dma_produced = 0U;
  rx_dma_size = n; rx_dma_callback = callback; rx_dma_argument = argument;
  return A_STATUS_OK; }
aStatus_t aDrvUsartRxDmaStart(aDrvUsartHandle_t *h, void *p, size_t n,
                              uint8_t priority,
                              aDrvUsartDmaCallback_t callback,
                              void *argument)
{ (void)h; (void)p; (void)priority; rx_dma_circular = A_FALSE; rx_dma_callback = callback;
  rx_dma_argument = argument; rx_dma_size = n; rx_dma_remaining = n;
  return A_STATUS_OK; }
aStatus_t aDrvUsartAsyncRxGetReceivedCount(aDrvUsartHandle_t *h, size_t *n)
{ (void)h;
  if (overwrite_during_copy && ++count_reads == 2U) {
      memset(rx_ring, 0xee, rx_dma_size);
      rx_dma_produced += rx_dma_size + 1U;
      overwrite_during_copy = A_FALSE;
  }
  *n = rx_dma_produced; return rx_dma_progress_status; }
aStatus_t aDrvUsartAsyncRxGetRemaining(aDrvUsartHandle_t *h, size_t *n)
{ (void)h; *n = rx_dma_remaining; return A_STATUS_OK; }
aStatus_t aDrvUsartAsyncRxStop(aDrvUsartHandle_t *h, size_t *n)
{ (void)h; ++rx_dma_stops; if (n) *n = rx_dma_circular
                                       ? rx_dma_produced : rx_dma_size - rx_dma_remaining;
  rx_dma_circular = A_FALSE;
  rx_dma_callback = NULL; return A_STATUS_OK; }

static void async_tx_callback(aDevUsartHandle_t *handle,
                              const aDevUsartTxEvent_t *event,
                              void *argument)
{ (void)handle; (void)argument; ++async_tx_callbacks;
  async_tx_status = event->status; }

static void async_rx_callback(aDevUsartHandle_t *handle,
                              const aDevUsartRxEvent_t *event,
                              void *argument)
{ (void)handle; (void)argument;
  assert(in_worker && locks == 0U);
  if (event->type == ADEV_USART_RX_EVENT_DATA_READY) {
      assert(event->buffer != rx_ring && event->offset == 0U);
      if (overwrite_in_callback) {
          uint8_t before[64];
          memcpy(before, event->buffer, event->length);
          memset(rx_ring, 0xee, rx_dma_size); /* DMA continues during callback. */
          rx_dma_produced += rx_dma_size;
          assert(memcmp(before, event->buffer, event->length) == 0);
          overwrite_in_callback = A_FALSE;
      }
  }
  ++async_rx_callbacks; async_rx_length = event->length;
  async_rx_reason = event->type;
}

static void queue_callback(aDevUsartTxQueueHandle_t *queue, uint32_t request_id,
                           const aDevUsartTxEvent_t *event, void *argument)
{
    (void)argument;
    assert(in_worker && locks == 0U);
    assert(aDevUsartTxQueueDeInit(queue) == A_STATUS_BUSY);
    assert(!aDevUsartTxQueueIsIdle(queue));
    assert(queue_callbacks < 4U);
    queue_callback_ids[queue_callbacks] = request_id;
    queue_callback_statuses[queue_callbacks++] = event->status;
}

static void fire(aDevUsartHandle_t *h, aDrvUsartExti_t event)
{
    assert(h->drv_handle.interrupt_enabled_mask & (1U << event));
    if (event == ADRV_USART_EXTI_TC) tc = A_TRUE;
    h->drv_handle.callbacks[event].function(h->drv_handle.callbacks[event].argument);
}

int main(void)
{
    aDevUsartConfig_t c;
    aDevUsartStorage_t storage;
    aDevUsartHandle_t *h = NULL;
    uint8_t ring[4], data[6] = {1,2,3,4,5,6}, received;
    const aDevUsartMode_t modes[] = {ADEV_USART_TX_POLLING,
        ADEV_USART_TX_INTERRUPT_BUFFERED, ADEV_USART_TX_DMA_BUFFERED};
    for (size_t i = 0; i < 3; ++i) {
        aDevUsartConfigStructInit(&c);
        assert(!c.rs485.enabled);
        c.mode = modes[i]; c.tx_buffer = ring; c.tx_buffer_size = sizeof(ring);
        c.rs485.enabled = A_TRUE; c.rs485.de_pin = 8; c.rs485.re_pin = 7;
        assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
        assert(levels[8] == ADRV_GPIO_LOW && levels[7] == ADRV_GPIO_LOW);
        byte_budget = 2;
        const aSSize_t n = aDevUsartWrite(h, data, sizeof(data), A_TIMEOUT_NO_WAIT);
        assert(n == (i == 0 ? 2 : 4)); /* Partial write leaves accepted data in flight. */
        assert(levels[8] == ADRV_GPIO_HIGH && levels[7] == ADRV_GPIO_HIGH);
        assert(aDevUsartRead(h, &received, 1, A_TIMEOUT_NO_WAIT) == 1);
        assert(levels[8] == ADRV_GPIO_HIGH); /* Read must not change direction. */
        assert(aDevUsartDeInit(h) == A_STATUS_BUSY);
        assert(aDevUsartWaitTransmitComplete(h, A_TIMEOUT_NO_WAIT) != A_STATUS_OK);
        assert(levels[8] == ADRV_GPIO_HIGH);
        byte_budget = 100;
        if (i == 1) while(h->tx_count) fire(h, ADRV_USART_EXTI_TXE);
        fire(h, ADRV_USART_EXTI_TC);
        assert(!h->rs485_transmitting && levels[8] == ADRV_GPIO_LOW);
        assert(levels[7] == ADRV_GPIO_LOW);

        if (i == 2) {
            /* Move tail, then wrap the next write into two DMA spans. */
            assert(aDevUsartWrite(h, data, 3, A_TIMEOUT_NO_WAIT) == 3);
            fire(h, ADRV_USART_EXTI_TC);
            assert(aDevUsartWrite(h, data, 3, A_TIMEOUT_NO_WAIT) == 3);
            fire(h, ADRV_USART_EXTI_TC);
            assert(h->rs485_transmitting && h->tx_count == 2);
            fire(h, ADRV_USART_EXTI_TC);
            assert(!h->rs485_transmitting && h->tx_count == 0);
        }

        assert(aDevUsartWriteDirect(h, data, 2, A_TIMEOUT_NO_WAIT) == 2);
        assert(dma_source == data); /* No bounce buffer. */
        assert(h->rs485_transmitting && levels[8] == ADRV_GPIO_HIGH);
        fire(h, ADRV_USART_EXTI_TC);
        assert(!h->rs485_transmitting);
        dma_stall = A_TRUE;
        assert(aDevUsartWriteDirect(h, data, 2, A_TIMEOUT_NO_WAIT) == -1);
        assert(last_error == A_STATUS_TIMEOUT && dma_remaining == 0);
        assert(h->rs485_transmitting); /* DMA stopped, final byte may still be shifting. */
        fire(h, ADRV_USART_EXTI_TC);
        dma_stall = A_FALSE;
        if (i == 2) {
            assert(aDevUsartWrite(h, data, 2, A_TIMEOUT_NO_WAIT) == 2);
            dma_error = A_TRUE;
            fire(h, ADRV_USART_EXTI_TC);
            assert(!h->rs485_transmitting && h->tx_dma_active == 0);
            assert(aDevUsartWaitTransmitComplete(h, A_TIMEOUT_NO_WAIT) == A_STATUS_ERROR);
            dma_error = A_FALSE;
        }
        assert(aDevUsartDeInit(h) == A_STATUS_OK);
        assert(mutex_count == 0 && locks == 0);
    }
    aDevUsartConfigStructInit(&c);
    c.rs485.enabled = A_TRUE; c.rs485.de_pin = c.drv_config.tx_pin;
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_INVALID_PARAM);
    c.rs485.de_pin = 8;
    irq_supported = A_FALSE;
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_UNSUPPORTED);
    assert(h == NULL && mutex_count == 0);
    irq_supported = A_TRUE;
    c.rs485.re_pin = 7;
    c.rs485.de_active_level = ADRV_GPIO_LOW;
    c.rs485.re_active_level = ADRV_GPIO_HIGH;
    c.rs485.receive_during_tx = A_TRUE;
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    assert(aDevUsartWrite(h, data, 1, A_TIMEOUT_NO_WAIT) == 1);
    assert(levels[8] == ADRV_GPIO_LOW && levels[7] == ADRV_GPIO_HIGH);
    fire(h, ADRV_USART_EXTI_TC);
    assert(levels[8] == ADRV_GPIO_HIGH);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    c.rs485.enabled = A_FALSE;
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    assert(aDevUsartWrite(h, data, 1, A_TIMEOUT_NO_WAIT) == 1);
    assert(!h->de_gpio.initialized && !h->rs485_transmitting);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);

    aDevUsartConfigStructInit(&c);
    h = NULL;
    assert(aDevUsartCreate(&c, &h) == A_STATUS_OK);
    assert(h != NULL);
    assert(aDevUsartDestroy(h) == A_STATUS_OK);
    assert(mutex_count == 0 && locks == 0);

    aDevUsartConfigStructInit(&c);
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    /* Ordinary Read returns available bytes without waiting to fill. */
    uint8_t read_buffer[8];
    rx_byte_budget = 2U;
    poll_waits = 0U;
    assert(aDevUsartRead(h, read_buffer, sizeof(read_buffer),
                         A_TIMEOUT_FOREVER) == 2);
    assert(poll_waits == 0U);
    assert(aDevUsartRead(h, read_buffer, sizeof(read_buffer),
                         A_TIMEOUT_NO_WAIT) == -1);
    assert(last_error == A_STATUS_TIMEOUT);
    rx_byte_budget = SIZE_MAX;

    /* Direct transfers to exactly the caller's buffer and stops before return. */
    direct_rx_count = sizeof(read_buffer);
    rx_dma_stops = 0U;
    assert(aDevUsartReadDirect(h, read_buffer, sizeof(read_buffer),
                               A_TIMEOUT_FOREVER) == sizeof(read_buffer));
    assert(direct_rx_target == read_buffer && read_buffer[0] == 0x5a);
    assert(rx_dma_stops == 1U);
    direct_rx_count = 3U;
    assert(aDevUsartReadDirect(h, read_buffer, sizeof(read_buffer),
                               A_TIMEOUT_MS(10U)) == 3);
    assert(rx_dma_stops == 2U);
    direct_rx_count = 0U;
    assert(aDevUsartReadDirect(h, read_buffer, sizeof(read_buffer),
                               A_TIMEOUT_MS(10U)) == -1);
    assert(rx_dma_stops == 3U && last_error == A_STATUS_TIMEOUT);
    assert(aDevUsartReadDirect(h, NULL, 0U, A_TIMEOUT_NO_WAIT) == 0);
    assert(aDevUsartRead(h, NULL, 0U, A_TIMEOUT_NO_WAIT) == 0);
    assert(rx_dma_stops == 3U);
    assert(aDevUsartReadDirect(h, read_buffer, sizeof(read_buffer),
                               A_TIMEOUT_NO_WAIT) == -1);
    assert(rx_dma_stops == 4U);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);

    c.mode = ADEV_USART_RX_INTERRUPT_BUFFERED;
    c.rx_buffer = ring;
    c.rx_buffer_size = sizeof(ring);
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    fire(h, ADRV_USART_EXTI_RXNE);
    fire(h, ADRV_USART_EXTI_RXNE);
    assert(aDevUsartReadDirect(h, read_buffer, sizeof(read_buffer),
                               A_TIMEOUT_MS(10U)) == -1);
    assert(last_error == A_STATUS_BUSY && rx_dma_stops == 4U);
    rx_waits = 0U;
    assert(aDevUsartRead(h, read_buffer, sizeof(read_buffer),
                         A_TIMEOUT_FOREVER) == 2);
    assert(rx_waits == 0U);
    direct_rx_count = sizeof(read_buffer);
    assert(aDevUsartReadDirect(h, read_buffer, sizeof(read_buffer),
                               A_TIMEOUT_FOREVER) == sizeof(read_buffer));
    assert(h->drv_handle.interrupt_enabled_mask & (1U << ADRV_USART_EXTI_RXNE));
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    aDevUsartConfigStructInit(&c);
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    async_tx_callbacks = 0U;
    assert(aDevUsartWriteAsync(h, NULL) == A_STATUS_INVALID_PARAM);
    assert(aDevUsartReadAsync(h, NULL, NULL) == A_STATUS_INVALID_PARAM);
    aDevUsartWriteRequest_t tx_request = {
            .buffer = data,
            .size = 3U,
            .timeout = A_TIMEOUT_MS(100U),
            .callback = async_tx_callback,
            .argument = NULL
        };
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_OK);
    memset(&tx_request, 0, sizeof(tx_request));
    assert(dma_source == data && h->tx_state == ADEV_USART_TX_ASYNC);
    fire(h, ADRV_USART_EXTI_TC);
    drain_work();
    assert(async_tx_callbacks == 1U && async_tx_status == A_STATUS_OK);

    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    uint8_t rx_a[8];
    aDevUsartConfigStructInit(&c);
    c.mode = ADEV_USART_RX_DMA_BUFFERED | ADEV_USART_OPTION_RX_IDLE;
    c.rx_buffer = rx_a;
    c.rx_buffer_size = sizeof(rx_a);
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    aDevUsartReadRequest_t rx_request = {
        .timeout = A_TIMEOUT_FOREVER, .callback = async_rx_callback,
    };
    aDevUsartReadToken_t first, second;
    async_rx_callbacks = 0U;
    assert(aDevUsartReadAsync(h, &rx_request, &first) == A_STATUS_OK);
    assert(aDevUsartReadAsync(h, &rx_request, &second) == A_STATUS_OK);
    assert(first != second);
    assert(aDevUsartDeInit(h) == A_STATUS_BUSY);
    fire(h, ADRV_USART_EXTI_IDLE);
    drain_work();
    assert(async_rx_callbacks == 0U);
    memset(rx_a, 0x5a, sizeof(rx_a));
    rx_dma_produced = 3U;
    fire(h, ADRV_USART_EXTI_IDLE);
    assert(async_rx_callbacks == 0U);
    drain_work();
    assert(async_rx_callbacks == 1U && async_rx_length == 3U);
    assert(async_rx_reason == ADEV_USART_RX_EVENT_DATA_READY);
    assert(aDevUsartReadAsyncCancel(h, second) == A_STATUS_OK);
    assert(aDevUsartReadAsyncCancel(h, second) == A_STATUS_BUSY);
    drain_work();
    assert(async_rx_callbacks == 2U && async_rx_reason == ADEV_USART_RX_EVENT_CANCELLED);
    assert(aDevUsartReadAsyncCancel(h, second) == A_STATUS_NOT_READY);

    /* Read and Async share consumption, Direct cannot take the active DMA. */
    rx_dma_produced = 5U;
    assert(aDevUsartRead(h, read_buffer, sizeof(read_buffer), A_TIMEOUT_NO_WAIT) == 2);
    assert(aDevUsartReadDirect(h, read_buffer, sizeof(read_buffer), A_TIMEOUT_NO_WAIT) == -1);
    assert(last_error == A_STATUS_BUSY);
    rx_request.timeout = A_TIMEOUT_MS(10U);
    assert(aDevUsartReadAsync(h, &rx_request, &first) == A_STATUS_OK);
    drain_work();
    MockTimer *rx_timer = h->rx_deadline_timer;
    rx_timer->callback(rx_timer->argument);
    drain_work();
    assert(async_rx_callbacks == 2U);
    uptime_ms += 10U;
    rx_timer->callback(rx_timer->argument);
    drain_work();
    assert(async_rx_callbacks == 3U && async_rx_reason == ADEV_USART_RX_EVENT_TIMEOUT);

    rx_request.timeout = A_TIMEOUT_NO_WAIT;
    assert(aDevUsartReadAsync(h, &rx_request, &first) == A_STATUS_OK);
    drain_work();
    assert(async_rx_callbacks == 4U && async_rx_reason == ADEV_USART_RX_EVENT_TIMEOUT);
    rx_dma_produced = 6U;
    assert(aDevUsartReadAsync(h, &rx_request, &first) == A_STATUS_OK);
    overwrite_in_callback = A_TRUE;
    drain_work();
    assert(async_rx_callbacks == 5U && async_rx_reason == ADEV_USART_RX_EVENT_DATA_READY);
    assert(!overwrite_in_callback);

    /* Detect overwrite during the snapshot copy, don't return torn data. */
    count_reads = 0U;
    overwrite_during_copy = A_TRUE;
    assert(aDevUsartRead(h, read_buffer, sizeof(read_buffer), A_TIMEOUT_NO_WAIT) == -1);
    assert(last_error == A_STATUS_ERROR && aDevUsartHasRxOverflowed(h));
    aDevUsartClearRxOverflow(h);
    rx_request.timeout = A_TIMEOUT_FOREVER;
    assert(aDevUsartReadAsync(h, &rx_request, &first) == A_STATUS_OK);
    rx_dma_progress_status = A_STATUS_ERROR;
    rx_dma_callback(rx_dma_argument);
    drain_work();
    assert(async_rx_callbacks == 6U && async_rx_reason == ADEV_USART_RX_EVENT_ERROR);
    rx_dma_progress_status = A_STATUS_OK;
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    assert(mutex_count == 0U && locks == 0U);
    aDevUsartConfigStructInit(&c);
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    aDevUsartTxRequest_t queue_requests[2];
    aDevUsartTxQueueConfig_t queue_config;
    aDevUsartTxQueueHandle_t queue;
    uint32_t request_a, request_b;
    aDevUsartTxQueueConfigStructInit(&queue_config);
    queue_config.usart = h;
    queue_config.request_storage = queue_requests;
    queue_config.request_capacity = 2U;
    queue_config.callback = queue_callback;
    assert(aDevUsartTxQueueInit(&queue_config, &queue) == A_STATUS_OK);
    queue_callbacks = 0U;
    assert(aDevUsartTxQueueSubmit(&queue, NULL, NULL) == A_STATUS_INVALID_PARAM);
    assert(aDevUsartTxQueueSubmit(&queue, &(aDevUsartTxQueueRequest_t) {
            .buffer = data,
            .size = 2U,
            .timeout = A_TIMEOUT_MS(100U)
        }, &request_a) == A_STATUS_OK);
    aDevUsartTxQueueRequest_t queued_request = {
            .buffer = data + 2U,
            .size = 2U,
            .timeout = A_TIMEOUT_MS(100U)
        };
    assert(aDevUsartTxQueueSubmit(&queue, &queued_request, &request_b) == A_STATUS_OK);
    memset(&queued_request, 0, sizeof(queued_request));
    assert(aDevUsartTxQueueGetPendingCount(&queue) == 2U);
    drain_work(); /* Queue starts DMA only from the aOS worker. */
    assert(aDevUsartWrite(h, data, 1U, A_TIMEOUT_NO_WAIT) == -1);
    assert(last_error == A_STATUS_BUSY);
    fire(h, ADRV_USART_EXTI_TC);
    drain_work();
    assert(queue_callbacks == 1U && queue_callback_ids[0] == request_a);
    fire(h, ADRV_USART_EXTI_TC);
    drain_work();
    assert(queue_callbacks == 2U && queue_callback_ids[1] == request_b);
    assert(queue_callback_statuses[0] == A_STATUS_OK &&
           queue_callback_statuses[1] == A_STATUS_OK);
    assert(aDevUsartTxQueueIsIdle(&queue));
    assert(aDevUsartTxQueueWaitDrained(&queue, A_TIMEOUT_MS(10U)) == A_STATUS_OK);
    assert(aDevUsartTxQueueDeInit(&queue) == A_STATUS_OK);

    assert(aDevUsartTxQueueInit(&queue_config, &queue) == A_STATUS_OK);
    queue_callbacks = 0U;
    uint32_t request_c, request_d;
    assert(aDevUsartTxQueueSubmit(&queue, &(aDevUsartTxQueueRequest_t) {
            .buffer = data,
            .size = 2U,
            .timeout = A_TIMEOUT_MS(100U)
        }, &request_c) == A_STATUS_OK);
    assert(aDevUsartTxQueueSubmit(&queue, &(aDevUsartTxQueueRequest_t) {
            .buffer = data + 2U,
            .size = 2U,
            .timeout = A_TIMEOUT_MS(100U)
        }, &request_d) == A_STATUS_OK);
    assert(aDevUsartTxQueueCancelAll(&queue) == A_STATUS_OK);
    drain_work();
    assert(queue_callbacks == 2U);
    assert(queue_callback_ids[0] == request_c &&
           queue_callback_ids[1] == request_d);
    assert(queue_callback_statuses[0] == A_STATUS_CANCELLED &&
           queue_callback_statuses[1] == A_STATUS_CANCELLED);
    assert(aDevUsartTxQueueDeInit(&queue) == A_STATUS_OK);
    /* Failure, queued expiry and cancellation all remain deferred. */
    assert(aDevUsartTxQueueInit(&queue_config, &queue) == A_STATUS_OK);
    queue_callbacks = 0U;
    aDevUsartTxQueueRequest_t late = {
        .buffer = data, .size = 2U, .timeout = A_TIMEOUT_MS(5U),
    };
    assert(aDevUsartTxQueueSubmit(&queue, &late, NULL) == A_STATUS_OK);
    assert(queue_callbacks == 0U);
    uptime_ms += 5U;
    drain_work();
    assert(queue_callbacks == 1U && queue_callback_statuses[0] == A_STATUS_TIMEOUT);
    queue_callbacks = 0U;
    late.timeout = A_TIMEOUT_FOREVER;
    late.size = 65536U; /* Rejected by device start before accessing payload. */
    assert(aDevUsartTxQueueSubmit(&queue, &late, NULL) == A_STATUS_OK);
    assert(queue_callbacks == 0U);
    drain_work();
    assert(queue_callbacks == 1U && queue_callback_statuses[0] == A_STATUS_INVALID_PARAM);
    queue_callbacks = 0U;
    late.size = 2U;
    assert(aDevUsartTxQueueSubmit(&queue, &late, NULL) == A_STATUS_OK);
    assert(aDevUsartTxQueueSubmit(&queue, &late, NULL) == A_STATUS_OK);
    assert(aDevUsartTxQueueSubmit(&queue, &late, NULL) == A_STATUS_BUSY);
    drain_work(); /* First DMA is now active. */
    assert(aDevUsartTxQueueCancelAll(&queue) == A_STATUS_OK);
    assert(queue_callbacks == 0U);
    drain_work();
    assert(queue_callbacks == 2U);
    assert(queue_callback_statuses[0] == A_STATUS_CANCELLED);
    assert(queue_callback_statuses[1] == A_STATUS_CANCELLED);
    assert(aDevUsartTxQueueDeInit(&queue) == A_STATUS_OK);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    assert(mutex_count == 0U && locks == 0U);
    puts("RS485 USART tests passed");
    return 0;
}
