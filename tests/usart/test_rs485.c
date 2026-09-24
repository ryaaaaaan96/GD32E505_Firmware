/* Host test: real device implementation, simulated nonblocking driver and OS. */
#include "aDev_usart_internal.h"
#include "aOS.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static aDrvGpioLevel_t levels[256];
static aBool_t irq_supported = A_TRUE;
static aBool_t tc = A_TRUE;
static aBool_t dma_stall;
static aBool_t dma_error;
static size_t dma_remaining;
static size_t byte_budget = 100;
static unsigned int mutex_count;
static unsigned int locks;
static aStatus_t last_error;
static const void *dma_source;

uint32_t aOSGetUptimeMs(void) { return 0; }
aStatus_t aOSValidateIsrPriority(uint32_t priority)
{ return priority >= 5U ? A_STATUS_OK : A_STATUS_INVALID_PARAM; }
void aOSYield(void) {}
aStatus_t aOSWaitObjectCreate(void **p) { *p = p; return A_STATUS_OK; }
void aOSWaitObjectDestroy(void **p) { *p = NULL; }
aStatus_t aOSWaitObjectWait(void *p, aTimeout_t t)
{ (void)p; (void)t; return A_STATUS_TIMEOUT; }
void aOSWaitObjectNotifyFromISR(void *p) { (void)p; }
aStatus_t aOSMutexCreate(void **p) { ++mutex_count; *p = p; return A_STATUS_OK; }
void aOSMutexDestroy(void **p) { if (*p) --mutex_count; *p = NULL; }
aStatus_t aOSMutexLock(void *p, aTimeout_t t)
{ (void)t; assert(p); assert(locks == 0); ++locks; return A_STATUS_OK; }
aStatus_t aOSMutexUnlock(void *p)
{ assert(p); assert(locks == 1); --locks; return A_STATUS_OK; }
aSSize_t aOSFailWithStatus(aStatus_t s) { last_error = s; return -1; }
aSSize_t aOSFailWithTimeout(aTimeout_t t)
{ (void)t; last_error = A_STATUS_TIMEOUT; return -1; }
aBool_t aOSPollWaitExpired(const aTimepoint_t *p) { (void)p; return A_TRUE; }

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
{ (void)h; *b = 42; return A_STATUS_OK; }
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
aStatus_t aDrvUsartAsyncRxCircularStart(aDrvUsartHandle_t *h, void *p, size_t n,
                                         uint8_t priority,
                                         aDrvUsartAsyncRxCallback_t callback,
                                         void *argument)
{ (void)h; (void)p; (void)n; (void)priority; (void)callback; (void)argument; return A_STATUS_OK; }
aStatus_t aDrvUsartAsyncRxGetReceivedCount(aDrvUsartHandle_t *h, size_t *n)
{ (void)h; *n = 0; return A_STATUS_OK; }

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
        assert(h.rs485_transmitting); /* DMA stopped, final byte may still be shifting. */
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
    puts("RS485 USART tests passed");
    return 0;
}
