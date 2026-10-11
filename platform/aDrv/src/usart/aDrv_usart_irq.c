#include "aDrv_usart.h"

#include "aDrv_usart_internal.h"

static uint32_t interrupt_value(aDrvUsartId_t id,
                                aDrvUsartEvent_t event)
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

    return id == ADRV_USART_5 ? usart5[event] : regular[event];
}

static void interrupt_config(aDrvUsartHandle_t *handle,
                             aDrvUsartEvent_t event, aBool_t enabled)
{
    if (event == ADRV_USART_EVENT_DISPATCH) return;
    const uint32_t interrupt = interrupt_value(handle->id, event);

    if (event == ADRV_USART_EVENT_RX_ERROR) {
        if (handle->id == ADRV_USART_5) {
            if (enabled) usart5_interrupt_enable((uint32_t)handle->instance,
                                                 USART5_INT_PERR);
            else usart5_interrupt_disable((uint32_t)handle->instance,
                                            USART5_INT_PERR);
        } else {
            if (enabled) usart_interrupt_enable((uint32_t)handle->instance,
                                                USART_INT_PERR);
            else usart_interrupt_disable((uint32_t)handle->instance,
                                           USART_INT_PERR);
        }
    }

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

static aBool_t has_registered_callback(const aDrvUsartHandle_t *handle)
{
    size_t index;

    for (index = 0U; index < ADRV_USART_EVENT_MAX; ++index) {
        if (handle->callbacks[index].function != NULL) {
            return A_TRUE;
        }
    }
    return A_FALSE;
}

aStatus_t aDrvUsartRequestDispatch(aDrvUsartHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->initialized ||
        !(handle->interrupt_enabled_mask & (1UL << ADRV_USART_EVENT_DISPATCH)) ||
        handle->callbacks[ADRV_USART_EVENT_DISPATCH].function == NULL)
        return A_STATUS_NOT_READY;
    handle->software_pending = A_TRUE;
    __DMB();
    NVIC_SetPendingIRQ(aDrvPrivateUsartMappingGet(handle->id)->irq);
    return A_STATUS_OK;
}

aBool_t aDrvUsartEventIsSupported(void)
{
    return A_TRUE;
}

aStatus_t aDrvUsartRegisterCallback(
    aDrvUsartHandle_t *handle, const aDrvUsartCallbackConfig_t *config)
{
    const aDrvPrivateUsartMapping_t *mapping;
    aStatus_t status;

    if ((handle == NULL) || (config == NULL) ||
        (config->event >= ADRV_USART_EVENT_MAX) ||
        (config->callback == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (((config->event == ADRV_USART_EVENT_TX_READY) &&
         (((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_TX) != 0U)) ||
        ((config->event == ADRV_USART_EVENT_RX_READY) &&
         (((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_RX) != 0U))) {
        return A_STATUS_BUSY;
    }

    status = aDrvPrivateUsartOwnerAcquire(
        handle, ADRV_USART_OWNER_INTERRUPT);
    if (status != A_STATUS_OK) {
        return status;
    }

    mapping = aDrvPrivateUsartMappingGet(handle->id);
    handle->callbacks[config->event].function = config->callback;
    handle->callbacks[config->event].argument = config->argument;
    if (config->enabled) {
        handle->interrupt_enabled_mask |= 1UL << config->event;
    } else {
        handle->interrupt_enabled_mask &= ~(1UL << config->event);
    }
    nvic_irq_enable(mapping->irq, handle->irq_priority, 0U);
    interrupt_config(handle, config->event, config->enabled);
    return A_STATUS_OK;
}

aStatus_t aDrvUsartUnregisterCallback(aDrvUsartHandle_t *handle,
                                      aDrvUsartEvent_t event)
{
    if ((handle == NULL) || (event >= ADRV_USART_EVENT_MAX)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (((uint32_t)handle->owner & ADRV_USART_OWNER_INTERRUPT) == 0U) {
        return A_STATUS_NOT_READY;
    }

    interrupt_config(handle, event, A_FALSE);
    handle->interrupt_enabled_mask &= ~(1UL << event);
    if (event == ADRV_USART_EVENT_DISPATCH) handle->software_pending = A_FALSE;
    handle->callbacks[event].function = NULL;
    handle->callbacks[event].argument = NULL;
    if (!has_registered_callback(handle)) {
        aDrvUsartPauseDispatch(handle);
        aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_INTERRUPT);
    }
    return A_STATUS_OK;
}

aStatus_t aDrvUsartSetEventEnabled(aDrvUsartHandle_t *handle,
                                   aDrvUsartEvent_t event,
                                   aBool_t enabled)
{
    if ((handle == NULL) || (event >= ADRV_USART_EVENT_MAX)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if ((((uint32_t)handle->owner & ADRV_USART_OWNER_INTERRUPT) == 0U) ||
        (handle->callbacks[event].function == NULL)) {
        return A_STATUS_NOT_READY;
    }

    interrupt_config(handle, event, enabled);
    if (enabled) {
        handle->interrupt_enabled_mask |= 1UL << event;
    } else {
        handle->interrupt_enabled_mask &= ~(1UL << event);
    }
    return A_STATUS_OK;
}

void aDrvUsartResumeDispatch(aDrvUsartHandle_t *handle)
{
    const aDrvPrivateUsartMapping_t *mapping;

    if ((handle == NULL) || (handle->initialized == 0U) ||
        (((uint32_t)handle->owner & ADRV_USART_OWNER_INTERRUPT) == 0U)) {
        return;
    }

    mapping = aDrvPrivateUsartMappingGet(handle->id);
    nvic_irq_enable(mapping->irq, handle->irq_priority, 0U);
}

void aDrvUsartPauseDispatch(aDrvUsartHandle_t *handle)
{
    const aDrvPrivateUsartMapping_t *mapping;

    if ((handle == NULL) || (handle->initialized == 0U)) {
        return;
    }

    mapping = aDrvPrivateUsartMappingGet(handle->id);
    nvic_irq_disable(mapping->irq);
}

static aBool_t interrupt_pending(const aDrvUsartHandle_t *handle,
                                 aDrvUsartEvent_t event)
{
    if (event == ADRV_USART_EVENT_RX_ERROR) {
        if (handle->id == ADRV_USART_5) {
            return usart5_interrupt_flag_get(handle->instance,
                       USART5_INT_FLAG_ERR_ORERR) != RESET ||
                   usart5_interrupt_flag_get(handle->instance,
                       USART5_INT_FLAG_ERR_NERR) != RESET ||
                   usart5_interrupt_flag_get(handle->instance,
                       USART5_INT_FLAG_ERR_FERR) != RESET ||
                   usart5_interrupt_flag_get(handle->instance,
                       USART5_INT_FLAG_PERR) != RESET;
        }
        return usart_interrupt_flag_get(handle->instance,
                   USART_INT_FLAG_ERR_ORERR) != RESET ||
               usart_interrupt_flag_get(handle->instance,
                   USART_INT_FLAG_ERR_NERR) != RESET ||
               usart_interrupt_flag_get(handle->instance,
                   USART_INT_FLAG_ERR_FERR) != RESET ||
               usart_interrupt_flag_get(handle->instance,
                   USART_INT_FLAG_PERR) != RESET;
    }
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
                   (usart5_interrupt_flag_enum)usart5[event]) != RESET;
    }

    return usart_interrupt_flag_get(
               (uint32_t)handle->instance,
               (usart_interrupt_flag_enum)regular[event]) != RESET;
}

static void invoke_callback(aDrvUsartHandle_t *handle,
                            aDrvUsartEvent_t event)
{
    const aDrvUsartCallback_t callback = handle->callbacks[event];

    if (((handle->interrupt_enabled_mask & (1UL << event)) != 0U) &&
        (callback.function != NULL) && interrupt_pending(handle, event)) {
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

    if (handle->software_pending &&
        (handle->interrupt_enabled_mask & (1UL << ADRV_USART_EVENT_DISPATCH))) {
        handle->software_pending = A_FALSE;
        const aDrvUsartCallback_t callback =
            handle->callbacks[ADRV_USART_EVENT_DISPATCH];
        if (callback.function != NULL) callback.function(callback.argument);
    }

    /* 先报告并清除错误，避免 RXNE 读取把错误状态一并清掉。 */
    invoke_callback(handle, ADRV_USART_EVENT_RX_ERROR);
    invoke_callback(handle, ADRV_USART_EVENT_RX_READY);
    invoke_callback(handle, ADRV_USART_EVENT_TX_READY);
    invoke_callback(handle, ADRV_USART_EVENT_TX_COMPLETE);

    if (((handle->interrupt_enabled_mask &
          (1UL << ADRV_USART_EVENT_RX_IDLE)) != 0U) &&
        interrupt_pending(handle, ADRV_USART_EVENT_RX_IDLE)) {
        (void)usart_data_receive((uint32_t)handle->instance);
        if (handle->callbacks[ADRV_USART_EVENT_RX_IDLE].function != NULL) {
            handle->callbacks[ADRV_USART_EVENT_RX_IDLE].function(
                handle->callbacks[ADRV_USART_EVENT_RX_IDLE].argument);
        }
    }

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
