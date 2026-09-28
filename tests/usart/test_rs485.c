/* Host test: real device implementation, simulated nonblocking driver and OS. */
#include "aDev_usart_internal.h"
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
static aBool_t dma_supported = A_TRUE;
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
static aBool_t reject_lock;
static uint32_t timer_create_delay, timer_duration;
static aStatus_t last_error;
static const void *dma_source;
static aDrvUsartDmaCallback_t rx_dma_callback;
static void *rx_dma_argument;
static size_t rx_dma_size;
static size_t rx_dma_remaining;
static aBool_t rx_dma_circular;
static size_t rx_dma_produced;
static aStatus_t rx_dma_progress_status = A_STATUS_OK;
static aBool_t complete_rx_on_wait;
static aBool_t overwrite_in_callback;
static uint8_t *rx_ring;
static aBool_t overwrite_during_copy;
static unsigned int count_reads;
static unsigned int async_tx_callbacks;
static unsigned int async_rx_callbacks;
static aDevUsartRxEventType_t async_rx_reason;
static aStatus_t async_tx_status;
static size_t async_rx_length;
static uint32_t uptime_ms;
typedef struct {
    aOSTimerCallback_t callback;
    void *argument;
} MockTimer;

void *aOSAlloc(size_t size) { return malloc(size); }
void aOSFree(void *memory) { free(memory); }

uint32_t aOSGetUptimeMs(void) { return uptime_ms; }
aStatus_t aOSValidateIsrPriority(uint32_t priority)
{ return priority >= 5U ? A_STATUS_OK : A_STATUS_INVALID_PARAM; }
void aOSYield(void) {}
aStatus_t aOSWaitObjectCreate(void **p) { *p = p; return A_STATUS_OK; }
void aOSWaitObjectDestroy(void **p) { *p = NULL; }
aStatus_t aOSWaitObjectWait(void *p, aTimeout_t t)
{
    (void)p; ++rx_waits;
    if (complete_rx_on_wait) {
        complete_rx_on_wait = A_FALSE;
        rx_dma_remaining = 0U;
        assert(rx_dma_callback != NULL);
        rx_dma_callback(rx_dma_argument);
        return A_STATUS_OK;
    }
    uptime_ms += t.milliseconds;
    return A_STATUS_TIMEOUT;
}
void aOSWaitObjectNotify(void *p) { (void)p; }
void aOSWaitObjectNotifyFromISR(void *p) { (void)p; }
aStatus_t aOSMutexCreate(void **p) { ++mutex_count; *p = p; return A_STATUS_OK; }
void aOSMutexDestroy(void **p) { if (*p) --mutex_count; *p = NULL; }
aStatus_t aOSMutexLock(void *p, aTimeout_t t)
{ assert(p); if (reject_lock) {
    assert(t.type == A_TIMEOUT_TYPE_RELATIVE && t.milliseconds == 0U);
    return A_STATUS_BUSY;
  } assert(locks < 4U); ++locks; return A_STATUS_OK; }
aStatus_t aOSMutexUnlock(void *p)
{ assert(p); assert(locks > 0U); --locks; return A_STATUS_OK; }
aStatus_t aOSTimerCreate(aOSTimer_t *timer, aOSTimerCallback_t callback,
                         void *argument)
{
    MockTimer *mock = malloc(sizeof(*mock));
    assert(mock != NULL);
    mock->callback = callback;
    mock->argument = argument;
    *timer = mock;
    uptime_ms += timer_create_delay;
    return A_STATUS_OK;
}
aStatus_t aOSTimerStart(aOSTimer_t timer, uint32_t milliseconds)
{ (void)timer; timer_duration = milliseconds; return A_STATUS_OK; }
aStatus_t aOSTimerStop(aOSTimer_t timer) { (void)timer; return A_STATUS_OK; }
aStatus_t aOSTimerDestroy(aOSTimer_t *timer)
{ free(*timer); *timer = NULL; return A_STATUS_OK; }
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
aBool_t aDrvUsartAsyncTxIsSupported(const aDrvUsartHandle_t *h) { (void)h; return dma_supported; }
aBool_t aDrvUsartAsyncRxIsSupported(const aDrvUsartHandle_t *h) { (void)h; return dma_supported; }
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
{ (void)priority; (void)aDrvUsartAsyncRxStart(h, p, n);
  rx_dma_circular = A_FALSE; rx_dma_callback = callback;
  rx_dma_argument = argument; rx_dma_size = n;
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
{ (void)handle; (void)argument; assert(locks == 0U); ++async_tx_callbacks;
  async_tx_status = event->status; }

static void async_rx_callback(aDevUsartHandle_t *handle,
                              const aDevUsartRxEvent_t *event,
                              void *argument)
{ (void)handle; (void)argument;
  assert(locks == 0U);
  if (event->type == ADEV_USART_RX_EVENT_DATA_READY) {
      assert(event->buffer >= (const void *)rx_ring && event->offset == 0U);
      if (overwrite_in_callback) {
          memset(rx_ring, 0xee, rx_dma_size); /* DMA continues during callback. */
          rx_dma_produced += rx_dma_size;
          overwrite_in_callback = A_FALSE;
      }
  }
  ++async_rx_callbacks; async_rx_length = event->length;
  async_rx_reason = event->type;
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
    /* Direct describes buffer ownership, not DMA. Defaults use polling. */
    dma_supported = A_FALSE;
    aDevUsartConfigStructInit(&c);
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    assert(aDevUsartIsSupported(h, ADEV_USART_CAP_TX_DIRECT));
    assert(aDevUsartIsSupported(h, ADEV_USART_CAP_RX_DIRECT));
    dma_source = NULL;
    direct_rx_target = NULL;
    byte_budget = 2U;
    assert(aDevUsartWriteDirect(h, data, sizeof(data), A_TIMEOUT_NO_WAIT) == 2);
    assert(dma_source == NULL && h->tx_count == 0U);
    assert(aDevUsartWriteDirect(h, data, 1U, A_TIMEOUT_NO_WAIT) == -1);
    assert(last_error == A_STATUS_TIMEOUT);
    uint8_t polled[4] = {0};
    rx_byte_budget = 2U;
    assert(aDevUsartReadDirect(h, polled, sizeof(polled), A_TIMEOUT_MS(5U)) == 2);
    assert(polled[0] == 42U && polled[1] == 42U && polled[2] == 0U);
    assert(direct_rx_target == NULL && h->rx_count == 0U);
    assert(aDevUsartReadDirect(h, polled, 1U, A_TIMEOUT_NO_WAIT) == -1);
    assert(aDevUsartReadDirect(h, NULL, 0U, A_TIMEOUT_NO_WAIT) == 0);
    assert(aDevUsartWriteDirect(h, NULL, 0U, A_TIMEOUT_NO_WAIT) == 0);
    const aDevUsartWriteRequest_t unsupported_async = {
        .buffer = data, .size = 1U, .timeout = A_TIMEOUT_MS(10U),
        .callback = async_tx_callback,
    };
    assert(aDevUsartWriteAsync(h, &unsupported_async) == A_STATUS_UNSUPPORTED);
    assert(dma_source == NULL);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    c.mode = 0x80000000U;
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_INVALID_PARAM);
    assert(h == NULL && mutex_count == 0U);
    c.mode = ADEV_USART_TX_DMA_BUFFERED;
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_UNSUPPORTED);
    assert(h == NULL && mutex_count == 0U);
    dma_supported = A_TRUE;
    byte_budget = 100U;
    rx_byte_budget = SIZE_MAX;
    const aDevUsartMode_t modes[] = {ADEV_USART_TX_POLLING,
        ADEV_USART_TX_INTERRUPT_BUFFERED, ADEV_USART_TX_DMA_BUFFERED};
    for (size_t i = 0; i < 3; ++i) {
        aDevUsartConfigStructInit(&c);
        assert(!c.rs485.enabled);
        c.mode = modes[i]; c.tx_buffer = ring; c.tx_buffer_size = sizeof(ring);
        c.rs485.enabled = A_TRUE; c.rs485.de_pin = 8;
        levels[7] = ADRV_GPIO_HIGH; /* Unrelated GPIO must never be touched. */
        assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
        assert(levels[8] == ADRV_GPIO_LOW && levels[7] == ADRV_GPIO_HIGH);
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
        assert(levels[7] == ADRV_GPIO_HIGH);

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

        if (i == 2) {
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
        }
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
    c.rs485.de_active_level = ADRV_GPIO_LOW;
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    assert(aDevUsartWrite(h, data, 1, A_TIMEOUT_NO_WAIT) == 1);
    assert(levels[8] == ADRV_GPIO_LOW && levels[7] == ADRV_GPIO_HIGH);
    fire(h, ADRV_USART_EXTI_TC);
    assert(levels[8] == ADRV_GPIO_HIGH);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    c.rs485.enabled = A_FALSE;
    levels[8] = ADRV_GPIO_LOW;
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    assert(aDevUsartWrite(h, data, 1, A_TIMEOUT_NO_WAIT) == 1);
    assert(!h->de_gpio.initialized && !h->rs485_transmitting);
    assert(levels[8] == ADRV_GPIO_LOW); /* TTL ignores the configured DE pin. */
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

    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    c.mode = ADEV_USART_RX_DMA_BUFFERED;
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    /* Direct transfers to exactly the caller's buffer and stops before return. */
    direct_rx_count = sizeof(read_buffer);
    rx_dma_stops = 0U;
    assert(aDevUsartReadDirect(h, read_buffer, sizeof(read_buffer),
                               A_TIMEOUT_FOREVER) == sizeof(read_buffer));
    assert(direct_rx_target == read_buffer && read_buffer[0] == 0x5a);
    assert(rx_dma_stops == 1U);
    direct_rx_count = 3U;
    unsigned waits_before = rx_waits;
    unsigned polls_before = poll_waits;
    assert(aDevUsartReadDirect(h, read_buffer, sizeof(read_buffer),
                               A_TIMEOUT_MS(10U)) == 3);
    assert(rx_waits > waits_before && poll_waits == polls_before);
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
    complete_rx_on_wait = A_TRUE;
    waits_before = rx_waits;
    assert(aDevUsartReadDirect(h, read_buffer, sizeof(read_buffer),
                               A_TIMEOUT_FOREVER) == sizeof(read_buffer));
    assert(!complete_rx_on_wait && rx_waits == waits_before + 1U);
    rx_dma_stops = 4U; /* Subsequent tests count their own stop operations. */
    assert(aDevUsartDeInit(h) == A_STATUS_OK);

    aDevUsartConfigStructInit(&c);
    c.mode = ADEV_USART_TX_DMA_BUFFERED;
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    async_tx_callbacks = 0U;
    assert(aDevUsartWriteAsync(h, NULL) == A_STATUS_INVALID_PARAM);
    assert(aDevUsartReadAsync(h, NULL) == A_STATUS_INVALID_PARAM);
    aDevUsartWriteRequest_t tx_request = {
            .buffer = data,
            .size = 3U,
            .timeout = A_TIMEOUT_MS(100U),
            .callback = async_tx_callback,
            .argument = NULL
        };
    reject_lock = A_TRUE;
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_BUSY);
    assert(locks == 0U && h->tx_deadline_timer == NULL);
    reject_lock = A_FALSE;
    timer_create_delay = 101U;
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_TIMEOUT);
    assert(h->tx_state == ADEV_USART_TX_IDLE && async_tx_callbacks == 0U && locks == 0U);
    assert(aOSTimerDestroy(&h->tx_deadline_timer) == A_STATUS_OK);
    timer_create_delay = 30U;
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_OK);
    assert(timer_duration == 70U);
    timer_create_delay = 0U;
    memset(&tx_request, 0, sizeof(tx_request));
    assert(dma_source == data && h->tx_state == ADEV_USART_TX_ASYNC);
    fire(h, ADRV_USART_EXTI_TC);
    assert(async_tx_callbacks == 1U && async_tx_status == A_STATUS_OK);

    tx_request = (aDevUsartWriteRequest_t) {
        .buffer = data, .size = 3U, .timeout = A_TIMEOUT_MS(10U),
        .callback = async_tx_callback,
    };
    dma_stall = A_TRUE;
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_OK);
    assert(aDevUsartWriteAsyncCancel(h) == A_STATUS_OK);
    assert(async_tx_callbacks == 2U && async_tx_status == A_STATUS_CANCELLED);
    assert(h->tx_state == ADEV_USART_TX_DRAINING);
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_BUSY);
    fire(h, ADRV_USART_EXTI_TC);
    assert(h->tx_state == ADEV_USART_TX_IDLE);
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_OK);
    MockTimer *tx_timer = h->tx_deadline_timer;
    tx_timer->callback(tx_timer->argument); /* Old/early expiry cannot finish new TX. */
    assert(async_tx_callbacks == 2U);
    uptime_ms += 10U;
    tx_timer->callback(tx_timer->argument);
    assert(async_tx_callbacks == 3U && async_tx_status == A_STATUS_TIMEOUT);
    fire(h, ADRV_USART_EXTI_TC);
    dma_stall = A_FALSE;

    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    uint8_t rx_a[8];
    aDevUsartConfigStructInit(&c);
    c.mode = ADEV_USART_RX_DMA_BUFFERED | ADEV_USART_OPTION_RX_IDLE;
    c.rx_buffer = rx_a;
    c.rx_buffer_size = sizeof(rx_a);
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    aDevUsartReadRequest_t rx_request = { .callback = async_rx_callback };
    async_rx_callbacks = 0U;
    assert(aDevUsartReadAsync(h, &rx_request) == A_STATUS_OK);
    assert(aDevUsartReadAsync(h, &rx_request) == A_STATUS_BUSY);
    assert(aDevUsartDeInit(h) == A_STATUS_BUSY);
    assert(aDevUsartRead(h, read_buffer, 1, A_TIMEOUT_NO_WAIT) == -1);
    assert(last_error == A_STATUS_BUSY);
    memset(rx_a, 0x5a, sizeof(rx_a));
    rx_dma_produced = 3U;
    fire(h, ADRV_USART_EXTI_IDLE);
    assert(async_rx_callbacks == 1U && async_rx_length == 3U);
    rx_dma_produced = 10U; /* Ring wrap: two contiguous callback spans. */
    fire(h, ADRV_USART_EXTI_IDLE);
    assert(async_rx_callbacks == 3U && async_rx_length == 2U);
    assert(aDevUsartReadAsyncCancel(h) == A_STATUS_OK);
    assert(async_rx_callbacks == 4U && async_rx_reason == ADEV_USART_RX_EVENT_CANCELLED);
    assert(aDevUsartReadAsyncCancel(h) == A_STATUS_NOT_READY);
    rx_dma_produced = 12U;
    assert(aDevUsartReadAsync(h, &rx_request) == A_STATUS_BUSY);
    assert(aDevUsartRead(h, read_buffer, sizeof(read_buffer), A_TIMEOUT_NO_WAIT) == 2);
    assert(aDevUsartReadAsync(h, &rx_request) == A_STATUS_OK);
    overwrite_in_callback = A_TRUE;
    rx_dma_produced = 13U;
    fire(h, ADRV_USART_EXTI_IDLE);
    assert(async_rx_reason == ADEV_USART_RX_EVENT_ERROR);
    assert(aDevUsartHasRxOverflowed(h));
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    aDevUsartConfigStructInit(&c);
    c.mode = ADEV_USART_RX_INTERRUPT_BUFFERED;
    c.rx_buffer = ring;
    c.rx_buffer_size = sizeof(ring);
    rx_ring = ring;
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    assert(aDevUsartReadAsync(h, &rx_request) == A_STATUS_OK);
    const unsigned before_irq_callback = async_rx_callbacks;
    fire(h, ADRV_USART_EXTI_RXNE);
    assert(async_rx_callbacks == before_irq_callback + 1U && async_rx_length == 1U);
    assert(h->rx_count == 0U);
    assert(aDevUsartReadAsyncCancel(h) == A_STATUS_OK);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    assert(mutex_count == 0U && locks == 0U);
    aDevUsartConfigStructInit(&c);
    assert(aDevUsartInitStatic(&c, &storage, &h) == A_STATUS_OK);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    assert(mutex_count == 0U && locks == 0U);
    puts("RS485 USART tests passed");
    return 0;
}
