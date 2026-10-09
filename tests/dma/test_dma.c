#include "aDrv_dma.h"
#include "aDrv_usart.h"
#include "gd32e50x.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t flags, remaining, interrupts;
    unsigned enabled;
} Channel;
static Channel channels[2][7];
static unsigned mask, in_isr, calls, events_seen;
static unsigned pending[12], irq_enabled[12];
static unsigned reload_reads, complete_on_enable;
uint32_t test_usart_data;

void DMA0_Channel0_IRQHandler(void);
void DMA0_Channel4_IRQHandler(void);
void DMA1_Channel2_IRQHandler(void);
void DMA1_Channel4_IRQHandler(void);

uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask = 1; }
void __enable_irq(void) { mask = 0; }
void __DMB(void) {}
void NVIC_ClearPendingIRQ(IRQn_Type irq) { pending[irq] = 0; }
void NVIC_SetPendingIRQ(IRQn_Type irq) { pending[irq] = 1; }
void nvic_irq_disable(IRQn_Type irq) { irq_enabled[irq] = 0; }
void nvic_irq_enable(IRQn_Type irq, unsigned priority, unsigned subpriority)
{ assert(priority <= 15 && subpriority == 0); irq_enabled[irq] = 1; }
void rcu_periph_clock_enable(rcu_periph_enum clock) { (void)clock; }
void dma_struct_para_init(dma_parameter_struct *p)
{ memset(p, 0, sizeof(*p)); }
void dma_deinit(uint32_t c, dma_channel_enum ch)
{ memset(&channels[c][ch], 0, sizeof(Channel)); }
void dma_init(uint32_t c, dma_channel_enum ch,
              const dma_parameter_struct *p) { (void)c; (void)ch; (void)p; }
void dma_memory_to_memory_enable(uint32_t c, dma_channel_enum ch)
{ (void)c; (void)ch; }
void dma_circulation_enable(uint32_t c, dma_channel_enum ch)
{ (void)c; (void)ch; }
void dma_circulation_disable(uint32_t c, dma_channel_enum ch)
{ (void)c; (void)ch; }
void dma_channel_disable(uint32_t c, dma_channel_enum ch)
{ channels[c][ch].enabled = 0; }
void dma_channel_enable(uint32_t c, dma_channel_enum ch)
{
    channels[c][ch].enabled = 1;
    if (complete_on_enable) {
        channels[c][ch].remaining = 0;
        channels[c][ch].flags = DMA_FLAG_FTF;
    }
}
void dma_memory_address_config(uint32_t c, dma_channel_enum ch, uint32_t a)
{ (void)c; (void)ch; (void)a; }
void dma_periph_address_config(uint32_t c, dma_channel_enum ch, uint32_t a)
{ (void)c; (void)ch; (void)a; }
void dma_transfer_number_config(uint32_t c, dma_channel_enum ch, uint32_t n)
{ channels[c][ch].remaining = n; }
uint32_t dma_transfer_number_get(uint32_t c, dma_channel_enum ch)
{
    if (reload_reads) {
        --reload_reads;
        channels[c][ch].flags |= DMA_FLAG_FTF;
    }
    return channels[c][ch].remaining;
}
unsigned dma_flag_get(uint32_t c, dma_channel_enum ch, uint32_t f)
{ return (channels[c][ch].flags & f) != 0; }
void dma_flag_clear(uint32_t c, dma_channel_enum ch, uint32_t f)
{ channels[c][ch].flags &= ~f; }
void dma_interrupt_disable(uint32_t c, dma_channel_enum ch, uint32_t f)
{ channels[c][ch].interrupts &= ~f; }
void dma_interrupt_enable(uint32_t c, dma_channel_enum ch, uint32_t f)
{ channels[c][ch].interrupts |= f; }
void usart_dma_transmit_config(uint32_t instance, unsigned enabled)
{ (void)instance; (void)enabled; }
void usart_dma_receive_config(uint32_t instance, unsigned enabled)
{ (void)instance; (void)enabled; }
aStatus_t aDrvPrivateUsartOwnerAcquire(aDrvUsartHandle_t *h,
                                       aDrvUsartOwner_t owner)
{ h->owner |= owner; return A_STATUS_OK; }
void aDrvPrivateUsartOwnerRelease(aDrvUsartHandle_t *h,
                                  aDrvUsartOwner_t owner)
{ h->owner &= ~owner; }

static void callback(void *arg, uint32_t events)
{
    assert(in_isr && arg == &calls);
    ++calls;
    events_seen |= events;
}
static void uart_callback(void *arg)
{ assert(in_isr && arg == &calls); ++calls; }
static void fire(void (*handler)(void))
{ in_isr = 1; handler(); in_isr = 0; }

static void test_dma(void)
{
    aDrvDmaConfig_t config;
    aDrvDmaHandle_t handle, other;
    aDrvDmaProgress_t progress;
    aDrvDmaInterruptConfig_t irq = {
        callback, &calls,
        ADRV_DMA_EVENT_HALF | ADRV_DMA_EVENT_COMPLETE | ADRV_DMA_EVENT_ERROR,
        5,
    };
    aDrvDmaConfigStructInit(&config);
    config.channel = 12;
    assert(aDrvDmaInitStatic(&config, &handle) == A_STATUS_INVALID_PARAM);
    config.channel = 0;
    config.circular = A_TRUE;
    assert(aDrvDmaInitStatic(&config, &handle) == A_STATUS_OK);
    assert(aDrvDmaInitStatic(&config, &other) == A_STATUS_BUSY);
    config.channel = 1;
    assert(aDrvDmaInitStatic(&config, &handle) == A_STATUS_BUSY);
    assert(aDrvDmaDstBufferLen(&handle, 65536U) == A_STATUS_INVALID_PARAM);
    assert(aDrvDmaDstBufferLen(&handle, 8) == A_STATUS_OK);
    irq.priority = 16;
    assert(aDrvDmaConfigureInterrupt(&handle, &irq) == A_STATUS_INVALID_PARAM);
    irq.priority = 5;
    assert(aDrvDmaConfigureInterrupt(&handle, &irq) == A_STATUS_OK);
    assert(aDrvDmaTransEnable(&handle) == A_STATUS_OK);
    channels[0][0].remaining = 4;
    channels[0][0].flags = DMA_FLAG_HTF;
    mask = 1;
    assert(aDrvDmaGetProgress(&handle, &progress) == A_STATUS_OK);
    assert(mask == 1 && progress.transferred == 4 && calls == 0);
    assert(pending[0]);
    mask = 0;
    fire(DMA0_Channel0_IRQHandler);
    assert(calls == 1 && events_seen == ADRV_DMA_EVENT_HALF);
    channels[0][0].remaining = 8;
    channels[0][0].flags = DMA_FLAG_FTF;
    fire(DMA0_Channel0_IRQHandler);
    assert(aDrvDmaGetProgress(&handle, &progress) == A_STATUS_OK);
    assert(progress.transferred == 8 && calls == 2);
    assert(aDrvDmaGetProgress(&handle, &progress) == A_STATUS_OK);
    assert(progress.transferred == 8);

    /* 模拟在读计数时发生重装，必须重试并且不重复计算同一标志。 */
    reload_reads = 1;
    assert(aDrvDmaGetProgress(&handle, &progress) == A_STATUS_OK);
    assert(progress.transferred == 16);
    reload_reads = 16;
    progress.transferred = 123;
    assert(aDrvDmaGetProgress(&handle, &progress) == A_STATUS_BUSY);
    assert(progress.transferred == 123 && mask == 0);
    fire(DMA0_Channel0_IRQHandler);
    channels[0][0].flags = DMA_FLAG_ERR;
    assert(aDrvDmaGetProgress(&handle, &progress) == A_STATUS_ERROR);
    assert(aDrvDmaGetProgress(&handle, &progress) == A_STATUS_ERROR);
    fire(DMA0_Channel0_IRQHandler);
    assert(events_seen & ADRV_DMA_EVENT_ERROR);
    assert(aDrvDmaDeInitStatic(&handle) == A_STATUS_OK);
    assert(!irq_enabled[0] && !pending[0]);
    const unsigned old_calls = calls;
    fire(DMA0_Channel0_IRQHandler);
    assert(calls == old_calls);

    /* 非 USART 使用 DMA1 CH4，同样有独立的中断分发入口。 */
    config.channel = 11;
    config.circular = A_FALSE;
    assert(aDrvDmaInitStatic(&config, &other) == A_STATUS_OK);
    assert(aDrvDmaDstBufferLen(&other, 3) == A_STATUS_OK);
    assert(aDrvDmaTransEnable(&other) == A_STATUS_OK);
    channels[1][4].remaining = 0;
    channels[1][4].flags = DMA_FLAG_FTF;
    /* 启动后注册不能清除已经完成的事件。 */
    assert(aDrvDmaConfigureInterrupt(&other, &irq) == A_STATUS_OK);
    fire(DMA1_Channel4_IRQHandler);
    assert(calls == old_calls + 1);
    assert(aDrvDmaGetProgress(&other, &progress) == A_STATUS_OK);
    assert(progress.transferred == 3);
    assert(aDrvDmaDeInitStatic(&other) == A_STATUS_OK);
}

static void test_usart(void)
{
    aDrvUsartHandle_t uart = {
        .id = ADRV_USART_0, .instance = USART0, .initialized = A_TRUE,
    };
    aDrvUsartHandle_t shared = {
        .id = ADRV_USART_3, .instance = UART3, .initialized = A_TRUE,
    };
    aDrvUsartHandle_t conflict = {
        .id = ADRV_USART_5, .instance = USART5, .initialized = A_TRUE,
    };
    aDrvUsartRxProgress_t progress;
    uint8_t buffer[8];
    size_t count;
    calls = 0;
    assert(aDrvUsartAsyncRxCircularStart(&uart, buffer, sizeof(buffer),
        5, uart_callback, &calls) == A_STATUS_OK);
    channels[0][4].remaining = 4;
    channels[0][4].flags = DMA_FLAG_HTF;
    assert(aDrvUsartAsyncRxGetProgress(&uart, &progress) == A_STATUS_OK);
    assert(progress.received == 4 && progress.position == 4 && calls == 0);
    fire(DMA0_Channel4_IRQHandler);
    assert(calls == 1);
    channels[0][4].remaining = 8;
    channels[0][4].flags = DMA_FLAG_FTF;
    fire(DMA0_Channel4_IRQHandler);
    assert(aDrvUsartAsyncRxGetProgress(&uart, &progress) == A_STATUS_OK);
    assert(progress.received == 8 && progress.position == 0);
    assert(aDrvUsartAsyncRxStop(&uart, &count) == A_STATUS_OK && count == 8);
    assert(aDrvUsartAsyncRxAbort(&uart) == A_STATUS_OK);
    complete_on_enable = 1;
    assert(aDrvUsartRxDmaStart(&uart, buffer, 1, 5,
        uart_callback, &calls) == A_STATUS_OK);
    fire(DMA0_Channel4_IRQHandler);
    assert(calls == 3);
    assert(aDrvUsartAsyncRxGetRemaining(&uart, &count) == A_STATUS_OK);
    assert(count == 0);
    complete_on_enable = 0;
    assert(aDrvUsartAsyncRxAbort(&uart) == A_STATUS_OK);

    assert(aDrvUsartAsyncRxCircularStart(&shared, buffer, 8,
        5, uart_callback, &calls) == A_STATUS_OK);
    assert(aDrvUsartAsyncRxCircularStart(&conflict, buffer, 8,
        5, uart_callback, &calls) == A_STATUS_BUSY);
    assert(aDrvUsartAsyncRxAbort(&shared) == A_STATUS_OK);
    assert(aDrvUsartAsyncRxCircularStart(&conflict, buffer, 8,
        5, uart_callback, &calls) == A_STATUS_OK);
    channels[1][2].flags = DMA_FLAG_HTF;
    fire(DMA1_Channel2_IRQHandler);
    assert(calls == 4);
    assert(aDrvUsartAsyncRxAbort(&conflict) == A_STATUS_OK);

    assert(aDrvUsartAsyncTxStart(&uart, buffer, 8, &count) == A_STATUS_OK);
    channels[0][3].remaining = 3;
    channels[0][3].flags = DMA_FLAG_ERR;
    assert(aDrvUsartAsyncTxGetRemaining(&uart, &count) == A_STATUS_ERROR);
    assert(count == 3 && !channels[0][3].enabled);
    assert(aDrvUsartAsyncTxAbort(&uart) == A_STATUS_OK);
}

int main(void)
{
    test_dma();
    test_usart();
    puts("DMA ownership, IRQ/progress races and USART integration passed");
    return 0;
}
