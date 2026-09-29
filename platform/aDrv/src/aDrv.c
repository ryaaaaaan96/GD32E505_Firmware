#include "aDrv.h"

#include "gd32e50x.h"

aStatus_t aDrvInit(void)
{
    /* Use all four priority bits for preemption. Set this before any
     * nvic_irq_enable() call: its default grouping reserves subpriority
     * bits, which violates the FreeRTOS ISR API priority contract. */
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);
    return A_STATUS_OK;
}
