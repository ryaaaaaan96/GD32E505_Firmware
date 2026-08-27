#include "aDrv_usart.h"

#include "aDrv_usart_internal.h"

static uint32_t interrupt_value(aDrvUsartId_t id,
                                aDrvUsartExti_t trigger)
{
    static const uint32_t regular[] = {
        USART_INT_TBE,
        USART_INT_RBNE,
        USART_INT_TC,
        USART_INT_IDLE,
        USART_INT_ERR,
    };
    static const uint32_t usart5[] = {
        USART5_INT_TBE,
        USART5_INT_RBNE,
        USART5_INT_TC,
        USART5_INT_IDLE,
        USART5_INT_ERR,
    };

    return id == ADRV_USART_5 ? usart5[trigger] : regular[trigger];
}

static void interrupt_config(aDrvUsartHandle_t *handle,
                             aDrvUsartExti_t trigger, bool enabled)
{
    const uint32_t interrupt = interrupt_value(handle->id, trigger);

    if (handle->id == ADRV_USART_5) {
        if (enabled) {
            usart5_interrupt_enable((uint32_t)handle->instance,
                                    (usart5_interrupt_enum)interrupt);
        } else {
            usart5_interrupt_disable((uint32_t)handle->instance,
                                     (usart5_interrupt_enum)interrupt);
        }
    } else if (enabled) {
        usart_interrupt_enable((uint32_t)handle->instance,
                               (usart_interrupt_enum)interrupt);
    } else {
        usart_interrupt_disable((uint32_t)handle->instance,
                                (usart_interrupt_enum)interrupt);
    }
}

static bool has_registered_callback(const aDrvUsartHandle_t *handle)
{
    size_t index;

    for (index = 0U; index < ADRV_USART_EXTI_MAX; ++index) {
        if (handle->callbacks[index].function != NULL) {
            return true;
        }
    }
    return false;
}

bool aDrvUsartInterruptIsSupported(void)
{
    return true;
}

aStatus_t aDrvUsartRegisterCallback(
    aDrvUsartHandle_t *handle, const aDrvUsartExtiConfig_t *config)
{
    const aDrvPrivateUsartMapping_t *mapping;
    aStatus_t status;

    if ((handle == NULL) || (config == NULL) ||
        (config->trigger >= ADRV_USART_EXTI_MAX) ||
        (config->priority > 15U) || (config->callback == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (((config->trigger == ADRV_USART_EXTI_TXE) &&
         (((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_TX) != 0U)) ||
        ((config->trigger == ADRV_USART_EXTI_RXNE) &&
         (((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_RX) != 0U))) {
        return A_STATUS_BUSY;
    }

    status = aDrvPrivateUsartOwnerAcquire(
        handle, ADRV_USART_OWNER_INTERRUPT);
    if (status != A_STATUS_OK) {
        return status;
    }

    mapping = aDrvPrivateUsartMappingGet(handle->id);
    handle->callbacks[config->trigger].function = config->callback;
    handle->callbacks[config->trigger].argument = config->argument;
    handle->irq_priority = (uint8_t)config->priority;
    nvic_irq_enable(mapping->irq, handle->irq_priority, 0U);
    interrupt_config(handle, config->trigger, config->enable != 0U);
    return A_STATUS_OK;
}

aStatus_t aDrvUsartUnregisterCallback(aDrvUsartHandle_t *handle,
                                      aDrvUsartExti_t trigger)
{
    if ((handle == NULL) || (trigger >= ADRV_USART_EXTI_MAX)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (((uint32_t)handle->owner & ADRV_USART_OWNER_INTERRUPT) == 0U) {
        return A_STATUS_NOT_READY;
    }

    interrupt_config(handle, trigger, false);
    handle->callbacks[trigger].function = NULL;
    handle->callbacks[trigger].argument = NULL;
    if (!has_registered_callback(handle)) {
        aDrvUsartDisableInterrupt(handle);
        aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_INTERRUPT);
    }
    return A_STATUS_OK;
}

aStatus_t aDrvUsartSetInterruptEnabled(aDrvUsartHandle_t *handle,
                                       aDrvUsartExti_t trigger,
                                       bool enabled)
{
    if ((handle == NULL) || (trigger >= ADRV_USART_EXTI_MAX)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if ((((uint32_t)handle->owner & ADRV_USART_OWNER_INTERRUPT) == 0U) ||
        (handle->callbacks[trigger].function == NULL)) {
        return A_STATUS_NOT_READY;
    }

    interrupt_config(handle, trigger, enabled);
    return A_STATUS_OK;
}

void aDrvUsartEnableInterrupt(aDrvUsartHandle_t *handle)
{
    const aDrvPrivateUsartMapping_t *mapping;

    if ((handle == NULL) || (handle->initialized == 0U) ||
        (((uint32_t)handle->owner & ADRV_USART_OWNER_INTERRUPT) == 0U)) {
        return;
    }

    mapping = aDrvPrivateUsartMappingGet(handle->id);
    nvic_irq_enable(mapping->irq, handle->irq_priority, 0U);
}

void aDrvUsartDisableInterrupt(aDrvUsartHandle_t *handle)
{
    const aDrvPrivateUsartMapping_t *mapping;

    if ((handle == NULL) || (handle->initialized == 0U)) {
        return;
    }

    mapping = aDrvPrivateUsartMappingGet(handle->id);
    nvic_irq_disable(mapping->irq);
}

static bool interrupt_pending(const aDrvUsartHandle_t *handle,
                              aDrvUsartExti_t trigger)
{
    static const uint32_t regular[] = {
        USART_INT_FLAG_TBE,
        USART_INT_FLAG_RBNE,
        USART_INT_FLAG_TC,
        USART_INT_FLAG_IDLE,
        USART_INT_FLAG_ERR_ORERR,
    };
    static const uint32_t usart5[] = {
        USART5_INT_FLAG_TBE,
        USART5_INT_FLAG_RBNE,
        USART5_INT_FLAG_TC,
        USART5_INT_FLAG_IDLE,
        USART5_INT_FLAG_ERR_ORERR,
    };

    if (handle->id == ADRV_USART_5) {
        return usart5_interrupt_flag_get(
                   (uint32_t)handle->instance,
                   (usart5_interrupt_flag_enum)usart5[trigger]) != RESET;
    }

    return usart_interrupt_flag_get(
               (uint32_t)handle->instance,
               (usart_interrupt_flag_enum)regular[trigger]) != RESET;
}

static void invoke_callback(aDrvUsartHandle_t *handle,
                            aDrvUsartExti_t trigger)
{
    const aDrvUsartCallback_t callback = handle->callbacks[trigger];

    if ((callback.function != NULL) && interrupt_pending(handle, trigger)) {
        callback.function(callback.argument);
    }
}

static void usart_irq_dispatch(aDrvUsartId_t id)
{
    aDrvUsartHandle_t *handle = aDrvPrivateUsartHandleGet(id);
    const aDrvPrivateUsartMapping_t *mapping =
        aDrvPrivateUsartMappingGet(id);

    if ((handle == NULL) || (handle->initialized == 0U) ||
        (((uint32_t)handle->owner & ADRV_USART_OWNER_INTERRUPT) == 0U)) {
        nvic_irq_disable(mapping->irq);
        return;
    }

    invoke_callback(handle, ADRV_USART_EXTI_RXNE);
    invoke_callback(handle, ADRV_USART_EXTI_TXE);
    invoke_callback(handle, ADRV_USART_EXTI_TC);

    if (interrupt_pending(handle, ADRV_USART_EXTI_IDLE)) {
        (void)usart_data_receive((uint32_t)handle->instance);
        if (handle->callbacks[ADRV_USART_EXTI_IDLE].function != NULL) {
            handle->callbacks[ADRV_USART_EXTI_IDLE].function(
                handle->callbacks[ADRV_USART_EXTI_IDLE].argument);
        }
    }

    invoke_callback(handle, ADRV_USART_EXTI_ERROR);
}

void USART0_IRQHandler(void)
{
    usart_irq_dispatch(ADRV_USART_0);
}

void USART1_IRQHandler(void)
{
    usart_irq_dispatch(ADRV_USART_1);
}

void USART2_IRQHandler(void)
{
    usart_irq_dispatch(ADRV_USART_2);
}

void UART3_IRQHandler(void)
{
    usart_irq_dispatch(ADRV_USART_3);
}

void UART4_IRQHandler(void)
{
    usart_irq_dispatch(ADRV_USART_4);
}

void USART5_IRQHandler(void)
{
    usart_irq_dispatch(ADRV_USART_5);
}
