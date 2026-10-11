/* Actual USART IRQ dispatcher; vendor register/NVIC operations are simulated. */
#include "aDrv_usart_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static aDrvUsartHandle_t handle;
static const aDrvPrivateUsartMapping_t mapping = {0};
static unsigned callbacks, register_writes, isr_depth;
static aBool_t pending, enabled, repend;
static unsigned hardware_event;
static unsigned configured_priority;

void USART0_IRQHandler(void);
void USART5_IRQHandler(void);
const aDrvPrivateUsartMapping_t *aDrvPrivateUsartMappingGet(aDrvUsartId_t id)
{ (void)id; return &mapping; }
aDrvUsartHandle_t *aDrvPrivateUsartHandleGet(aDrvUsartId_t id)
{ (void)id; return &handle; }
aStatus_t aDrvPrivateUsartOwnerAcquire(aDrvUsartHandle_t *h, aDrvUsartOwner_t owner)
{ h->owner |= owner; return A_STATUS_OK; }
void aDrvPrivateUsartOwnerRelease(aDrvUsartHandle_t *h, aDrvUsartOwner_t owner)
{ h->owner &= ~owner; }
void nvic_irq_enable(IRQn_Type irq, unsigned priority, unsigned subpriority)
{ (void)irq; assert(subpriority == 0U);
  configured_priority = priority; enabled = A_TRUE; }
void nvic_irq_disable(IRQn_Type irq) { (void)irq; enabled = A_FALSE; }
void NVIC_SetPendingIRQ(IRQn_Type irq) { (void)irq; pending = A_TRUE; }
void __DMB(void) {}
void usart_interrupt_enable(uint32_t instance, usart_interrupt_enum event)
{ (void)instance; (void)event; ++register_writes; }
void usart_interrupt_disable(uint32_t instance, usart_interrupt_enum event)
{ (void)instance; (void)event; ++register_writes; }
void usart5_interrupt_enable(uint32_t instance, usart5_interrupt_enum event)
{ usart_interrupt_enable(instance, event); }
void usart5_interrupt_disable(uint32_t instance, usart5_interrupt_enum event)
{ usart_interrupt_disable(instance, event); }
unsigned usart_interrupt_flag_get(uint32_t instance, usart_interrupt_flag_enum event)
{ (void)instance; return event == hardware_event ? SET : RESET; }
unsigned usart5_interrupt_flag_get(uint32_t instance, usart5_interrupt_flag_enum event)
{ return usart_interrupt_flag_get(instance, event); }
uint16_t usart_data_receive(uint32_t instance) { (void)instance; return 0; }

static void callback(void *argument)
{
    assert(argument == &handle && isr_depth == 1U);
    ++callbacks;
    if (repend) {
        repend = A_FALSE;
        assert(aDrvUsartRequestDispatch(&handle) == A_STATUS_OK);
    }
}

static void service(void)
{
    assert(enabled);
    pending = A_FALSE;
    ++isr_depth;
    if (handle.id == ADRV_USART_5) USART5_IRQHandler();
    else USART0_IRQHandler();
    --isr_depth;
}

int main(void)
{
    assert(aDrvUsartRequestDispatch(NULL) == A_STATUS_INVALID_PARAM);
    assert(aDrvUsartRequestDispatch(&handle) == A_STATUS_NOT_READY);
    for (unsigned variant = 0U; variant < 2U; ++variant) {
        memset(&handle, 0, sizeof(handle));
        handle.id = variant ? ADRV_USART_5 : ADRV_USART_0;
        handle.initialized = A_TRUE;
        handle.irq_priority = variant ? 11U : 6U;
        callbacks = register_writes = hardware_event = 0U;
        aDrvUsartCallbackConfig_t config = {
            .event = ADRV_USART_EVENT_DISPATCH,
            .callback = callback, .argument = &handle, .enabled = A_TRUE,
        };
        assert(aDrvUsartRegisterCallback(&handle, &config) == A_STATUS_OK);
        assert(configured_priority == (variant ? 11U : 6U));
        assert(register_writes == 0U); /* Software event is not a SPL enum. */
        assert(aDrvUsartRequestDispatch(&handle) == A_STATUS_OK);
        assert(aDrvUsartRequestDispatch(&handle) == A_STATUS_OK);
        assert(pending && callbacks == 0U);
        service();
        assert(callbacks == 1U && !handle.software_pending);
        service();
        assert(callbacks == 1U); /* Coalesced / spurious IRQ. */
        repend = A_TRUE;
        assert(aDrvUsartRequestDispatch(&handle) == A_STATUS_OK);
        service();
        assert(callbacks == 2U && pending);
        service();
        assert(callbacks == 3U && !pending);
        config.event = ADRV_USART_EVENT_TX_COMPLETE;
        assert(aDrvUsartRegisterCallback(&handle, &config) == A_STATUS_OK);
        config.event = ADRV_USART_EVENT_RX_IDLE;
        assert(aDrvUsartRegisterCallback(&handle, &config) == A_STATUS_OK);
        aDrvUsartPauseDispatch(&handle);
        aDrvUsartResumeDispatch(&handle);
        assert(configured_priority == (variant ? 11U : 6U));
        hardware_event = variant ? USART5_INT_FLAG_TC : USART_INT_FLAG_TC;
        assert(aDrvUsartRequestDispatch(&handle) == A_STATUS_OK);
        service();
        assert(callbacks == 5U); /* Software and hardware in the same IRQ. */
        assert(aDrvUsartUnregisterCallback(&handle, ADRV_USART_EVENT_DISPATCH)
               == A_STATUS_OK);
        assert(aDrvUsartRequestDispatch(&handle) == A_STATUS_NOT_READY);
    }
    puts("USART software IRQ dispatch tests passed");
    return 0;
}
