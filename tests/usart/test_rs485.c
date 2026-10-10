/* Host test: real device implementation, simulated nonblocking driver and OS. */
#include "aDev_usart_internal.h"
#include "aOS.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static aDrvGpioLevel_t levels[256];
static unsigned isr_depth;
static aBool_t irq_supported = A_TRUE;
static aBool_t tc = A_TRUE;
static aBool_t dma_stall;
static aBool_t hardware_rx_error;
static aBool_t dma_error;
static aBool_t dma_supported = A_TRUE;
static unsigned driver_init_count;
static unsigned driver_deinit_count;
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
static size_t rx_dma_position = SIZE_MAX;
static aStatus_t rx_dma_progress_status = A_STATUS_OK;
static aBool_t complete_rx_on_wait;
static aDevUsartHandle_t *error_on_wait;
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
static unsigned critical_entries, critical_depth;
static unsigned inject_on_exit;
static aDevUsartHandle_t *inject_handle;
static uint8_t rx_byte = 42U;
static void fire(aDevUsartHandle_t *h, aDrvUsartExti_t event);
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
    if (error_on_wait != NULL) {
        hardware_rx_error = A_TRUE;
        fire(error_on_wait, ADRV_USART_EXTI_ERROR);
        error_on_wait = NULL;
        return A_STATUS_OK;
    }
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
aStatus_t aOSMutexCreate(void **p) { assert(0); ++mutex_count; *p = p; return A_STATUS_OK; }
void aOSMutexDestroy(void **p) { assert(0); if (*p) --mutex_count; *p = NULL; }
aStatus_t aOSMutexLock(void *p, aTimeout_t t)
{ assert(0); assert(p); if (reject_lock) {
    assert(t.type == A_TIMEOUT_TYPE_RELATIVE && t.milliseconds == 0U);
    return A_STATUS_BUSY;
  } assert(locks < 4U); ++locks; return A_STATUS_OK; }
aStatus_t aOSMutexUnlock(void *p)
{ assert(0); assert(p); assert(locks > 0U); --locks; return A_STATUS_OK; }
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
void aOSCriticalEnter(void) { ++critical_entries; ++critical_depth; }
void aOSCriticalExit(void)
{
    assert(critical_depth != 0U);
    --critical_depth;
    if (inject_on_exit != 0U && --inject_on_exit == 0U) {
        assert(critical_depth == 0U);
        fire(inject_handle, ADRV_USART_EXTI_RXNE);
    }
}
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
aStatus_t aDrvGpioDeInit(aDrvGpioHandle_t *h) { ++driver_deinit_count; h->initialized = A_FALSE; return A_STATUS_OK; }
aStatus_t aDrvGpioWrite(const aDrvGpioHandle_t *h, aDrvGpioLevel_t l)
{ assert(h->initialized); levels[h->pin] = l; return A_STATUS_OK; }
void aDrvUsartConfigStructInit(aDrvUsartConfig_t *p)
{ memset(p, 0, sizeof(*p)); p->tx_pin = 9; p->rx_pin = 10; }
void aDrvUsartHandleStructInit(aDrvUsartHandle_t *p) { memset(p, 0, sizeof(*p)); }
aStatus_t aDrvUsartInitStatic(const aDrvUsartConfig_t *c, aDrvUsartHandle_t *h)
{ ++driver_init_count; h->id = c->id;
  h->initialized = A_TRUE; return A_STATUS_OK; }
aStatus_t aDrvUsartDeInitStatic(aDrvUsartHandle_t *h)
{ ++driver_deinit_count; h->initialized = A_FALSE; return A_STATUS_OK; }
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
{ if (aDrvUsartTakeRxError(h) != A_STATUS_OK) return A_STATUS_ERROR;
  if (!rx_byte_budget) return A_STATUS_BUSY;
  --rx_byte_budget; *b = rx_byte; return A_STATUS_OK; }
aStatus_t aDrvUsartIsTransmitComplete(const aDrvUsartHandle_t *h, aBool_t *v)
{ (void)h; *v = tc; return A_STATUS_OK; }
aBool_t aDrvUsartDmaTxIsSupported(aDrvUsartId_t id) { (void)id; return dma_supported; }
aBool_t aDrvUsartDmaRxIsSupported(aDrvUsartId_t id) { (void)id; return dma_supported; }
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
aStatus_t aDrvUsartAsyncRxGetProgress(aDrvUsartHandle_t *h,
                                      aDrvUsartRxProgress_t *progress)
{
    aStatus_t status = aDrvUsartAsyncRxGetReceivedCount(h,
                                                      &progress->received);
    progress->position = rx_dma_position != SIZE_MAX ? rx_dma_position :
                         progress->received % rx_dma_size;
    return status;
}
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
{ (void)handle; (void)argument; assert(isr_depth > 0U); assert(locks == 0U); ++async_tx_callbacks;
  async_tx_status = event->status; }

static void async_rx_callback(aDevUsartHandle_t *handle,
                              const aDevUsartRxEvent_t *event,
                              void *argument)
{ (void)handle; (void)argument;
  assert(isr_depth > 0U);
  assert(locks == 0U);
  if (event->type == ADEV_USART_RX_EVENT_DATA_READY) {
      assert(event->offset == 0U);
      assert(handle->rx.dma_active ? event->buffer == handle->rx.snapshot
                                   : event->buffer == handle->settings.rx_buffer + handle->rx.tail);
      if (overwrite_in_callback) {
          uint8_t saved[32];
          assert(event->length <= sizeof(saved));
          memcpy(saved, event->buffer, event->length);
          memset(rx_ring, 0xee, rx_dma_size); /* DMA continues during callback. */
          rx_dma_produced += rx_dma_size + 1U;
          assert(memcmp(saved, event->buffer, event->length) == 0);
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
    ++isr_depth;
    h->drv_handle.callbacks[event].function(h->drv_handle.callbacks[event].argument);
    --isr_depth;
}

aStatus_t aDrvUsartPendInterrupt(aDrvUsartHandle_t *h)
{
    h->software_pending = A_TRUE;
    return A_STATUS_OK;
}

static void service_pending(aDevUsartHandle_t *h)
{
    assert(h->drv_handle.software_pending);
    h->drv_handle.software_pending = A_FALSE;
    fire(h, ADRV_USART_EXTI_SOFTWARE);
}

/* 非 2 的幂容量、满缓冲、回绕、部分读取及复制窗口内的 ISR。 */
static void buffered_rx_tests(void)
{
    aDevUsartConfig_t config;
    aDevUsartHandle_t handle;
    uint8_t ring[7], output[7];
    unsigned before;

    aDevUsartConfigStructInit(&config);
    config.mode = ADEV_USART_RX_INTERRUPT_BUFFERED;
    config.rx_buffer = ring;
    config.rx_buffer_size = sizeof(ring);
    rx_byte_budget = SIZE_MAX;
    assert(aDevUsartInitStatic(&config, &handle) == A_STATUS_OK);
    for (rx_byte = 0U; rx_byte < 7U; ++rx_byte)
        fire(&handle, ADRV_USART_EXTI_RXNE);
    assert(handle.rx.head == 0U && handle.rx.count == 7U);
    inject_handle = &handle;
    inject_on_exit = 1U;
    before = critical_entries;
    assert(aDevUsartRead(&handle, output, sizeof(output),
                        A_TIMEOUT_NO_WAIT) == 7);
    assert(critical_entries - before == 2U);
    for (size_t i = 0U; i < 7U; ++i) assert(output[i] == i);
    /* 未提交前仍占满：到达的新字节丢弃，已占用的数据保持稳定。 */
    assert(aDevUsartHasRxOverflowed(&handle));
    aDevUsartClearRxOverflow(&handle);
    for (rx_byte = 10U; rx_byte < 15U; ++rx_byte)
        fire(&handle, ADRV_USART_EXTI_RXNE);
    assert(aDevUsartRead(&handle, output, 3U, A_TIMEOUT_NO_WAIT) == 3);
    for (size_t i = 0U; i < 3U; ++i) assert(output[i] == 10U + i);
    for (rx_byte = 15U; rx_byte < 18U; ++rx_byte)
        fire(&handle, ADRV_USART_EXTI_RXNE);
    /* 快照后再来一个字节，提交必须保留 ISR 新增的 rx_count。 */
    inject_on_exit = 1U;
    before = critical_entries;
    assert(aDevUsartRead(&handle, output, 6U, A_TIMEOUT_NO_WAIT) == 6);
    assert(critical_entries - before == 4U);
    for (size_t i = 0U; i < 6U; ++i) assert(output[i] == 13U + i);
    assert(handle.rx.count == 0U && !aDevUsartHasRxOverflowed(&handle));
    assert(aDevUsartRead(&handle, output, 1U, A_TIMEOUT_NO_WAIT) == -1);
    assert(aDevUsartDeInit(&handle) == A_STATUS_OK);
    assert(critical_depth == 0U && mutex_count == 0U && locks == 0U);
    rx_byte = 42U;
}

static unsigned byte_callbacks, byte_errors;
static void byte_receive(void *context, uint8_t byte, aStatus_t status)
{
    (void)byte;
    assert(context == &byte_callbacks);
    if (status == A_STATUS_OK) byte_callbacks++;
    else byte_errors++;
}

int main(void)
{
    aDevUsartConfig_t c;
    aDevUsartHandle_t instance;
    aDevUsartHandle_t *h = &instance;
    uint8_t ring[4], data[6] = {1,2,3,4,5,6}, received;
    /* Direct describes buffer ownership, not DMA. Defaults use polling. */
    dma_supported = A_FALSE;
    aDevUsartConfigStructInit(&c);
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
    assert(aDevUsartIsSupported(h, ADEV_USART_CAP_TX_DIRECT));
    assert(aDevUsartIsSupported(h, ADEV_USART_CAP_RX_DIRECT));
    dma_source = NULL;
    direct_rx_target = NULL;
    byte_budget = 2U;
    assert(aDevUsartWriteDirect(h, data, sizeof(data), A_TIMEOUT_NO_WAIT) == 2);
    assert(dma_source == NULL && h->tx.count == 0U);
    assert(aDevUsartWriteDirect(h, data, 1U, A_TIMEOUT_NO_WAIT) == -1);
    assert(last_error == A_STATUS_TIMEOUT);
    uint8_t polled[4] = {0};
    rx_byte_budget = 2U;
    assert(aDevUsartReadDirect(h, polled, sizeof(polled), A_TIMEOUT_MS(5U)) == 2);
    assert(polled[0] == 42U && polled[1] == 42U && polled[2] == 0U);
    assert(direct_rx_target == NULL && h->rx.count == 0U);
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
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_INVALID_PARAM);
    assert(!h->drv_handle.initialized && mutex_count == 0U);
    c.mode = ADEV_USART_TX_POLLING;
    c.rs485.mode = ADEV_USART_RS485_UART_DE;
    unsigned calls_before = driver_init_count;
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_UNSUPPORTED);
    assert(driver_init_count == calls_before);
    c.rs485.mode = (aDevUsartRS485Mode_t)99;
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_INVALID_PARAM);
    assert(driver_init_count == calls_before);
    c.rs485.mode = ADEV_USART_RS485_NONE;
    const unsigned init_count_before = driver_init_count;
    const unsigned deinit_count_before = driver_deinit_count;
    c.mode = ADEV_USART_TX_DMA_BUFFERED;
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_UNSUPPORTED);
    assert(driver_init_count == init_count_before);
    assert(driver_deinit_count == deinit_count_before);
    c.mode = ADEV_USART_RX_DMA_BUFFERED;
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_UNSUPPORTED);
    assert(driver_init_count == init_count_before);
    assert(driver_deinit_count == deinit_count_before);
    assert(!h->drv_handle.initialized && mutex_count == 0U);
    dma_supported = A_TRUE;
    byte_budget = 100U;
    rx_byte_budget = SIZE_MAX;
    const aDevUsartMode_t modes[] = {ADEV_USART_TX_POLLING,
        ADEV_USART_TX_INTERRUPT_BUFFERED, ADEV_USART_TX_DMA_BUFFERED};
    for (size_t i = 0; i < 3; ++i) {
        aDevUsartConfigStructInit(&c);
        assert((c.rs485.mode == ADEV_USART_RS485_NONE));
        c.mode = modes[i]; c.tx_buffer = ring; c.tx_buffer_size = sizeof(ring);
        c.rs485.mode = ADEV_USART_RS485_GPIO_DE; c.rs485.de_pin = 8;
        levels[7] = ADRV_GPIO_HIGH; /* Unrelated GPIO must never be touched. */
        assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
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
        if (i == 1) while(h->tx.count) fire(h, ADRV_USART_EXTI_TXE);
        fire(h, ADRV_USART_EXTI_TC);
        assert(!h->rs485_transmitting && levels[8] == ADRV_GPIO_LOW);
        assert(levels[7] == ADRV_GPIO_HIGH);

        if (i == 2) {
            /* Move tail, then wrap the next write into two DMA spans. */
            assert(aDevUsartWrite(h, data, 3, A_TIMEOUT_NO_WAIT) == 3);
            fire(h, ADRV_USART_EXTI_TC);
            assert(aDevUsartWrite(h, data, 3, A_TIMEOUT_NO_WAIT) == 3);
            fire(h, ADRV_USART_EXTI_TC);
            assert(h->rs485_transmitting && h->tx.count == 2);
            fire(h, ADRV_USART_EXTI_TC);
            assert(!h->rs485_transmitting && h->tx.count == 0);
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
            assert(!h->rs485_transmitting && h->tx.dma_active == 0);
            assert(aDevUsartWaitTransmitComplete(h, A_TIMEOUT_NO_WAIT) == A_STATUS_ERROR);
            dma_error = A_FALSE;
        }
        assert(aDevUsartDeInit(h) == A_STATUS_OK);
        assert(mutex_count == 0 && locks == 0);
    }
    aDevUsartConfigStructInit(&c);
    c.rs485.mode = ADEV_USART_RS485_GPIO_DE; c.rs485.de_pin = c.drv_config.tx_pin;
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_INVALID_PARAM);
    c.rs485.de_pin = 8;
    irq_supported = A_FALSE;
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_UNSUPPORTED);
    assert(!h->drv_handle.initialized && mutex_count == 0);
    irq_supported = A_TRUE;
    c.rs485.de_active_level = ADRV_GPIO_LOW;
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
    assert(aDevUsartWrite(h, data, 1, A_TIMEOUT_NO_WAIT) == 1);
    assert(levels[8] == ADRV_GPIO_LOW && levels[7] == ADRV_GPIO_HIGH);
    fire(h, ADRV_USART_EXTI_TC);
    assert(levels[8] == ADRV_GPIO_HIGH);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    c.rs485.mode = ADEV_USART_RS485_NONE;
    levels[8] = ADRV_GPIO_LOW;
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
    assert(aDevUsartWrite(h, data, 1, A_TIMEOUT_NO_WAIT) == 1);
    assert(!h->de_gpio.initialized && !h->rs485_transmitting);
    assert(levels[8] == ADRV_GPIO_LOW); /* TTL ignores the configured DE pin. */
    assert(aDevUsartDeInit(h) == A_STATUS_OK);

    aDevUsartConfigStructInit(&c);
    h = NULL;
    assert(aDevUsartCreate(&c, &h) == A_STATUS_OK);
    assert(h != NULL);
    assert(aDevUsartDestroy(h) == A_STATUS_OK);
    h = &instance;
    assert(mutex_count == 0 && locks == 0);

    aDevUsartConfigStructInit(&c);
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
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
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
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
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
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
    /* 活动事务仍返回 BUSY，不依赖任务互斥锁。 */
    h->tx.state = ADEV_USART_TX_STREAM;
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_BUSY);
    assert(locks == 0U && h->tx.deadline_timer == NULL);
    h->tx.state = ADEV_USART_TX_IDLE;
    timer_create_delay = 101U;
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_TIMEOUT);
    assert(h->tx.state == ADEV_USART_TX_IDLE && async_tx_callbacks == 0U && locks == 0U);
    assert(aOSTimerDestroy(&h->tx.deadline_timer) == A_STATUS_OK);
    timer_create_delay = 30U;
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_OK);
    assert(timer_duration == 70U);
    timer_create_delay = 0U;
    memset(&tx_request, 0, sizeof(tx_request));
    assert(dma_source == data && h->tx.state == ADEV_USART_TX_ASYNC);
    fire(h, ADRV_USART_EXTI_TC);
    assert(async_tx_callbacks == 1U && async_tx_status == A_STATUS_OK);

    tx_request = (aDevUsartWriteRequest_t) {
        .buffer = data, .size = 3U, .timeout = A_TIMEOUT_MS(10U),
        .callback = async_tx_callback,
    };
    dma_stall = A_TRUE;
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_OK);
    assert(aDevUsartWriteAsyncCancel(h) == A_STATUS_OK);
    assert(async_tx_callbacks == 1U);
    assert(aDevUsartDeInit(h) == A_STATUS_BUSY);
    assert(aDevUsartWriteAsyncCancel(h) == A_STATUS_NOT_READY);
    service_pending(h);
    fire(h, ADRV_USART_EXTI_SOFTWARE); /* Duplicate IRQ cannot redeliver. */
    assert(async_tx_callbacks == 2U && async_tx_status == A_STATUS_CANCELLED);
    assert(h->tx.state == ADEV_USART_TX_DRAINING);
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_BUSY);
    fire(h, ADRV_USART_EXTI_TC);
    assert(h->tx.state == ADEV_USART_TX_IDLE);
    assert(aDevUsartWriteAsync(h, &tx_request) == A_STATUS_OK);
    MockTimer *tx_timer = h->tx.deadline_timer;
    tx_timer->callback(tx_timer->argument); /* Old/early expiry cannot finish new TX. */
    assert(async_tx_callbacks == 2U);
    uptime_ms += 10U;
    tx_timer->callback(tx_timer->argument);
    assert(async_tx_callbacks == 2U);
    tx_timer->callback(tx_timer->argument);
    fire(h, ADRV_USART_EXTI_TC); /* Hardware finishes before queued callback. */
    assert(async_tx_callbacks == 2U);
    service_pending(h);
    assert(async_tx_callbacks == 3U && async_tx_status == A_STATUS_TIMEOUT);
    assert(h->tx.state == ADEV_USART_TX_IDLE);
    dma_stall = A_FALSE;

    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    uint8_t rx_a[8];
    aDevUsartConfigStructInit(&c);
    c.mode = ADEV_USART_RX_DMA_BUFFERED | ADEV_USART_OPTION_RX_IDLE;
    c.rx_buffer = rx_a;
    c.rx_buffer_size = sizeof(rx_a);
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
    uint8_t snapshot[8];
    aDevUsartReadRequest_t rx_request = {
        .buffer = snapshot, .buffer_size = sizeof(snapshot),
        .callback = async_rx_callback,
    };
    rx_request.buffer = NULL;
    assert(aDevUsartReadAsync(h, &rx_request) == A_STATUS_INVALID_PARAM);
    rx_request.buffer = rx_a + 1U;
    assert(aDevUsartReadAsync(h, &rx_request) == A_STATUS_INVALID_PARAM);
    rx_request.buffer = snapshot;
    rx_request.buffer_size = sizeof(snapshot) - 1U;
    assert(aDevUsartReadAsync(h, &rx_request) == A_STATUS_INVALID_PARAM);
    rx_request.buffer_size = sizeof(snapshot);
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
    rx_a[0] = 8U; rx_a[1] = 9U;
    rx_dma_produced = 10U; /* Ring wrap is combined into a stable snapshot. */
    fire(h, ADRV_USART_EXTI_IDLE);
    assert(async_rx_callbacks == 2U && async_rx_length == 7U);
    const uint8_t wrapped[] = {0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 8, 9};
    assert(memcmp(snapshot, wrapped, sizeof(wrapped)) == 0);
    assert(aDevUsartReadAsyncCancel(h) == A_STATUS_OK);
    assert(async_rx_callbacks == 2U);
    assert(aDevUsartReadAsyncCancel(h) == A_STATUS_BUSY);
    assert(aDevUsartDeInit(h) == A_STATUS_BUSY);
    fire(h, ADRV_USART_EXTI_IDLE); /* Data must not pass a pending cancellation. */
    assert(async_rx_callbacks == 2U);
    service_pending(h);
    fire(h, ADRV_USART_EXTI_SOFTWARE);
    assert(async_rx_callbacks == 3U && async_rx_reason == ADEV_USART_RX_EVENT_CANCELLED);
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

    /* A torn copy must never be delivered as DATA_READY. */
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
    assert(aDevUsartReadAsync(h, &rx_request) == A_STATUS_OK);
    rx_dma_produced = 3U;
    overwrite_during_copy = A_TRUE;
    count_reads = 0U;
    const unsigned before_torn_copy = async_rx_callbacks;
    fire(h, ADRV_USART_EXTI_IDLE);
    assert(async_rx_callbacks == before_torn_copy + 1U);
    assert(async_rx_reason == ADEV_USART_RX_EVENT_ERROR);
    assert(h->rx.state == ADEV_USART_RX_IDLE && h->rx.snapshot == NULL);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    aDevUsartConfigStructInit(&c);
    c.mode = ADEV_USART_RX_INTERRUPT_BUFFERED;
    c.rx_buffer = ring;
    c.rx_buffer_size = sizeof(ring);
    rx_ring = ring;
    rx_request.buffer = NULL;
    rx_request.buffer_size = 0U;
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
    assert(aDevUsartReadAsync(h, &rx_request) == A_STATUS_OK);
    const unsigned before_irq_callback = async_rx_callbacks;
    fire(h, ADRV_USART_EXTI_RXNE);
    assert(async_rx_callbacks == before_irq_callback + 1U && async_rx_length == 1U);
    assert(h->rx.count == 0U);
    assert(aDevUsartReadAsyncCancel(h) == A_STATUS_OK);
    service_pending(h);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    assert(mutex_count == 0U && locks == 0U);
    aDevUsartConfigStructInit(&c);
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    assert(mutex_count == 0U && locks == 0U);
    /* 累计计数跨 SIZE_MAX；物理游标必须独立推进。 */
    {
        uint8_t rollover_ring[3] = {0x11U, 0x22U, 0x33U};
        uint8_t value = 0U;
        aDevUsartConfigStructInit(&c);
        c.mode = ADEV_USART_RX_DMA_BUFFERED;
        c.rx_buffer = rollover_ring;
        c.rx_buffer_size = sizeof(rollover_ring);
        assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
        h->rx.dma_consumed = SIZE_MAX;
        h->rx.tail = SIZE_MAX % sizeof(rollover_ring);
        rx_dma_produced = 0U;
        assert(aDevUsartRead(h, &value, 1U, A_TIMEOUT_NO_WAIT) == 1);
        assert(value == 0x11U);
        rx_dma_produced = 1U;
        assert(aDevUsartRead(h, &value, 1U, A_TIMEOUT_NO_WAIT) == 1);
        assert(value == 0x22U);
        /* 已累计回绕后发生覆盖，必须使用 DMA 的物理位置恢复。 */
        rx_dma_produced = 7U;
        rx_dma_position = 2U;
        assert(aDevUsartRead(h, &value, 1U, A_TIMEOUT_NO_WAIT) == 1);
        assert(value == 0x33U);
        assert(aDevUsartHasRxOverflowed(h));
        aDevUsartClearRxOverflow(h);
        aDevUsartClearRxError(h);
        assert(aDevUsartRead(h, &value, 1U, A_TIMEOUT_NO_WAIT) == 1);
        assert(value == 0x11U);
        rx_dma_position = SIZE_MAX;
        assert(aDevUsartDeInit(h) == A_STATUS_OK);
    }
    aDevUsartConfigStructInit(&c);
    c.mode = ADEV_USART_RX_INTERRUPT_CALLBACK;
    c.rx_byte_callback = byte_receive;
    c.rx_byte_context = &byte_callbacks;
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
    fire(h, ADRV_USART_EXTI_RXNE);
    assert(byte_callbacks == 1U);
    hardware_rx_error = A_TRUE;
    fire(h, ADRV_USART_EXTI_ERROR);
    assert(byte_errors == 1U && !hardware_rx_error);
    assert(aDevUsartGetRxError(h) == A_STATUS_ERROR);
    aDevUsartClearRxError(h);
    assert(aDevUsartGetRxError(h) == A_STATUS_OK);
    assert(aDevUsartRead(h, read_buffer, 1U, A_TIMEOUT_NO_WAIT) == -1);
    assert(last_error == A_STATUS_UNSUPPORTED);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    aDevUsartConfigStructInit(&c);
    c.mode = ADEV_USART_RX_INTERRUPT_BUFFERED;
    c.rx_buffer = ring;
    c.rx_buffer_size = sizeof(ring);
    assert(aDevUsartInitStatic(&c, h) == A_STATUS_OK);
    error_on_wait = h;
    assert(aDevUsartRead(h, read_buffer, 1U, A_TIMEOUT_MS(100U)) == -1);
    assert(last_error == A_STATUS_ERROR && error_on_wait == NULL);
    aDevUsartClearRxError(h);
    fire(h, ADRV_USART_EXTI_RXNE);
    assert(aDevUsartRead(h, read_buffer, 1U, A_TIMEOUT_NO_WAIT) == 1);
    assert(aDevUsartDeInit(h) == A_STATUS_OK);
    buffered_rx_tests();
    puts("RS485 USART tests passed");
    return 0;
}

aStatus_t aDrvUsartTakeRxError(aDrvUsartHandle_t *h)
{
    (void)h;
    aBool_t error = hardware_rx_error;
    hardware_rx_error = A_FALSE;
    return error ? A_STATUS_ERROR : A_STATUS_OK;
}
