#include "aDrv_dma.h"

#include "aDrv_internal.h"
#include <string.h>

typedef struct {
    uintptr_t controller;
    uint8_t channel;
} dmaMapping_t;

static const dmaMapping_t dma_mappings[] = {
    {DMA0, DMA_CH0},
    {DMA0, DMA_CH1},
    {DMA0, DMA_CH2},
    {DMA0, DMA_CH3},
    {DMA0, DMA_CH4},
    {DMA0, DMA_CH5},
    {DMA0, DMA_CH6},
    {DMA1, DMA_CH0},
    {DMA1, DMA_CH1},
    {DMA1, DMA_CH2},
    {DMA1, DMA_CH3},
    {DMA1, DMA_CH4},
};

static aDrvDmaHandle_t *s_dma_owners[ADRV_ARRAY_COUNT(dma_mappings)];

static IRQn_Type dma_irq(const aDrvDmaHandle_t *handle)
{
    return (IRQn_Type)((handle->controller == DMA0
                           ? DMA0_Channel0_IRQn : DMA1_Channel0_IRQn) +
                      handle->channel);
}

static uint32_t dma_critical_enter(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return primask;
}

static void dma_critical_exit(uint32_t primask)
{
    __DMB();
    if ((primask & 1U) == 0U) {
        __enable_irq();
    }
}

static aStatus_t resolve_dma(aDrvDmaChannel_t channel, dmaMapping_t *mapping)
{
    if ((mapping == NULL) ||
        ((size_t)channel >= ADRV_ARRAY_COUNT(dma_mappings))) {
        return A_STATUS_INVALID_PARAM;
    }

    *mapping = dma_mappings[channel];
    return A_STATUS_OK;
}

static uint32_t peripheral_width(aDrvDmaWidth_t width)
{
    switch (width) {
    case ADRV_DMA_WIDTH_8:
        return DMA_PERIPHERAL_WIDTH_8BIT;
    case ADRV_DMA_WIDTH_16:
        return DMA_PERIPHERAL_WIDTH_16BIT;
    case ADRV_DMA_WIDTH_32:
        return DMA_PERIPHERAL_WIDTH_32BIT;
    default:
        return DMA_PERIPHERAL_WIDTH_8BIT;
    }
}

static uint32_t memory_width(aDrvDmaWidth_t width)
{
    switch (width) {
    case ADRV_DMA_WIDTH_8:
        return DMA_MEMORY_WIDTH_8BIT;
    case ADRV_DMA_WIDTH_16:
        return DMA_MEMORY_WIDTH_16BIT;
    case ADRV_DMA_WIDTH_32:
        return DMA_MEMORY_WIDTH_32BIT;
    default:
        return DMA_MEMORY_WIDTH_8BIT;
    }
}

void aDrvDmaConfigStructInit(aDrvDmaConfig_t *config)
{
    if (config == NULL) {
        return;
    }

    config->channel = ADRV_DMA_CHANNEL_NONE;
    config->direction = ADRV_DMA_DIR_PERIPH_TO_MEMORY;
    config->periphWidth = ADRV_DMA_WIDTH_8;
    config->memoryWidth = ADRV_DMA_WIDTH_8;
    config->priority = ADRV_DMA_PRIORITY_LOW;
    config->periphIncrement = A_FALSE;
    config->memoryIncrement = A_TRUE;
    config->circular = A_FALSE;
}

void aDrvDmaHandleStructInit(aDrvDmaHandle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    memset(handle, 0, sizeof(*handle));
}

aStatus_t aDrvDmaInitStatic(const aDrvDmaConfig_t *config,
                            aDrvDmaHandle_t *handle)
{
    static const uint32_t priorities[] = {
        DMA_PRIORITY_LOW,
        DMA_PRIORITY_MEDIUM,
        DMA_PRIORITY_HIGH,
        DMA_PRIORITY_ULTRA_HIGH,
    };
    dma_parameter_struct parameters;
    dmaMapping_t mapping;
    uint32_t critical_state;

    if ((config == NULL) || (handle == NULL) ||
        ((size_t)config->priority >= ADRV_ARRAY_COUNT(priorities)) ||
        ((unsigned)config->direction > ADRV_DMA_DIR_MEMORY_TO_MEMORY) ||
        ((unsigned)config->periphWidth > ADRV_DMA_WIDTH_32) ||
        ((unsigned)config->memoryWidth > ADRV_DMA_WIDTH_32) ||
        (resolve_dma(config->channel, &mapping) != A_STATUS_OK)) {
        return A_STATUS_INVALID_PARAM;
    }

    rcu_periph_clock_enable(mapping.controller == DMA0 ? RCU_DMA0 : RCU_DMA1);

    critical_state = dma_critical_enter();
    if (s_dma_owners[config->channel] != NULL) {
        dma_critical_exit(critical_state);
        return A_STATUS_BUSY;
    }
    for (size_t i = 0U; i < ADRV_ARRAY_COUNT(dma_mappings); ++i) {
        if (s_dma_owners[i] == handle) {
            dma_critical_exit(critical_state);
            return A_STATUS_BUSY;
        }
    }
    aDrvDmaHandleStructInit(handle);
    handle->controller = mapping.controller;
    handle->channel = mapping.channel;
    nvic_irq_disable(dma_irq(handle));
    NVIC_ClearPendingIRQ(dma_irq(handle));
    s_dma_owners[config->channel] = handle;
    dma_critical_exit(critical_state);

    dma_struct_para_init(&parameters);
    parameters.periph_width = peripheral_width(config->periphWidth);
    parameters.memory_width = memory_width(config->memoryWidth);
    parameters.periph_inc = config->periphIncrement
                                ? DMA_PERIPH_INCREASE_ENABLE
                                : DMA_PERIPH_INCREASE_DISABLE;
    parameters.memory_inc = config->memoryIncrement
                                ? DMA_MEMORY_INCREASE_ENABLE
                                : DMA_MEMORY_INCREASE_DISABLE;
    parameters.direction = config->direction == ADRV_DMA_DIR_MEMORY_TO_PERIPH
                               ? DMA_MEMORY_TO_PERIPHERAL
                               : DMA_PERIPHERAL_TO_MEMORY;
    parameters.priority = priorities[config->priority];

    dma_deinit((uint32_t)mapping.controller,
               (dma_channel_enum)mapping.channel);
    dma_init((uint32_t)mapping.controller,
             (dma_channel_enum)mapping.channel, &parameters);

    if (config->direction == ADRV_DMA_DIR_MEMORY_TO_MEMORY) {
        dma_memory_to_memory_enable((uint32_t)mapping.controller,
                                    (dma_channel_enum)mapping.channel);
    }
    if (config->circular != 0U) {
        dma_circulation_enable((uint32_t)mapping.controller,
                               (dma_channel_enum)mapping.channel);
    }

    handle->controller = mapping.controller;
    handle->channel = mapping.channel;
    handle->circular = config->circular;
    handle->initialized = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aDrvDmaDeInitStatic(aDrvDmaHandle_t *handle)
{
    uint32_t channel_index;
    uint32_t critical_state;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    channel_index = (handle->controller == DMA0 ? 0U : 7U) +
                    (uint32_t)handle->channel;
    critical_state = dma_critical_enter();
    if ((channel_index >= ADRV_ARRAY_COUNT(dma_mappings)) ||
        (s_dma_owners[channel_index] != handle) ||
        (dma_mappings[channel_index].controller != handle->controller) ||
        (dma_mappings[channel_index].channel != handle->channel)) {
        dma_critical_exit(critical_state);
        return A_STATUS_ERROR;
    }
    nvic_irq_disable(dma_irq(handle));
    NVIC_ClearPendingIRQ(dma_irq(handle));
    dma_channel_disable((uint32_t)handle->controller,
                        (dma_channel_enum)handle->channel);
    dma_deinit((uint32_t)handle->controller,
               (dma_channel_enum)handle->channel);
    s_dma_owners[channel_index] = NULL;
    dma_critical_exit(critical_state);
    aDrvDmaHandleStructInit(handle);
    return A_STATUS_OK;
}

aStatus_t aDrvDmaSrcBufferSet(aDrvDmaHandle_t *handle, const void *source)
{
    if ((handle == NULL) || (source == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    dma_memory_address_config((uint32_t)handle->controller,
                              (dma_channel_enum)handle->channel,
                              (uint32_t)(uintptr_t)source);
    return A_STATUS_OK;
}

aStatus_t aDrvDmaDstBufferSet(aDrvDmaHandle_t *handle, void *destination)
{
    if ((handle == NULL) || (destination == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    dma_periph_address_config((uint32_t)handle->controller,
                              (dma_channel_enum)handle->channel,
                              (uint32_t)(uintptr_t)destination);
    return A_STATUS_OK;
}

aStatus_t aDrvDmaDstBufferLen(aDrvDmaHandle_t *handle, uint32_t length)
{
    if ((handle == NULL) || (length == 0U) || (length > 65535U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    dma_transfer_number_config((uint32_t)handle->controller,
                               (dma_channel_enum)handle->channel, length);
    handle->length = length;
    return A_STATUS_OK;
}

aStatus_t aDrvDmaTransDisable(aDrvDmaHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    dma_channel_disable((uint32_t)handle->controller,
                        (dma_channel_enum)handle->channel);
    return A_STATUS_OK;
}

aStatus_t aDrvDmaTransEnable(aDrvDmaHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (handle->length == 0U) return A_STATUS_INVALID_PARAM;
    /* 新事务先清除旧事件；中断配置保留，缓冲区已由调用者设置。 */
    const uint32_t key = dma_critical_enter();
    dma_flag_clear((uint32_t)handle->controller,
                   (dma_channel_enum)handle->channel, DMA_FLAG_G);
    NVIC_ClearPendingIRQ(dma_irq(handle));
    handle->cycles = 0U;
    handle->pending_events = 0U;
    handle->error = A_FALSE;
    dma_channel_enable((uint32_t)handle->controller,
                       (dma_channel_enum)handle->channel);
    dma_critical_exit(key);
    return A_STATUS_OK;
}

aStatus_t aDrvDmaCircularSet(aDrvDmaHandle_t *handle, aBool_t enabled)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    if (enabled) {
        dma_circulation_enable((uint32_t)handle->controller,
                               (dma_channel_enum)handle->channel);
    } else {
        dma_circulation_disable((uint32_t)handle->controller,
                                (dma_channel_enum)handle->channel);
    }
    handle->circular = enabled;
    return A_STATUS_OK;
}

uint32_t aDrvDmaCurLenGet(const aDrvDmaHandle_t *handle)
{
    if ((handle == NULL) || (handle->initialized == 0U)) {
        return 0U;
    }

    return dma_transfer_number_get((uint32_t)handle->controller,
                                   (dma_channel_enum)handle->channel);
}

/* ISR 和查询路径共用一次标志消费；查询只挂起通知，不执行用户回调。 */
static void events_collect(aDrvDmaHandle_t *handle)
{
    const uint32_t controller = (uint32_t)handle->controller;
    const dma_channel_enum channel = (dma_channel_enum)handle->channel;
    uint32_t events = 0U;

    if (dma_flag_get(controller, channel, DMA_FLAG_FTF) != RESET) {
        dma_flag_clear(controller, channel, DMA_FLAG_FTF);
        ++handle->cycles;
        events |= ADRV_DMA_EVENT_COMPLETE;
    }
    if (dma_flag_get(controller, channel, DMA_FLAG_HTF) != RESET) {
        dma_flag_clear(controller, channel, DMA_FLAG_HTF);
        events |= ADRV_DMA_EVENT_HALF;
    }
    if (dma_flag_get(controller, channel, DMA_FLAG_ERR) != RESET) {
        dma_flag_clear(controller, channel, DMA_FLAG_ERR);
        handle->error = A_TRUE;
        events |= ADRV_DMA_EVENT_ERROR;
    }
    handle->pending_events |= events & handle->interrupt.events;
}

void aDrvDmaInterruptConfigStructInit(aDrvDmaInterruptConfig_t *config)
{
    if (config == NULL) return;
    config->callback = NULL;
    config->argument = NULL;
    config->events = ADRV_DMA_EVENT_COMPLETE | ADRV_DMA_EVENT_ERROR;
    config->priority = 5U;
}

aStatus_t aDrvDmaConfigureInterrupt(
    aDrvDmaHandle_t *handle, const aDrvDmaInterruptConfig_t *config)
{
    const uint32_t valid = ADRV_DMA_EVENT_HALF | ADRV_DMA_EVENT_COMPLETE |
                           ADRV_DMA_EVENT_ERROR;
    uint32_t sources = 0U;
    uint32_t key;

    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (config != NULL && (config->callback == NULL ||
        config->priority > 15U || config->events == 0U ||
        (config->events & ~valid) != 0U)) return A_STATUS_INVALID_PARAM;
    if (!handle->initialized) return A_STATUS_NOT_READY;
    key = dma_critical_enter();
    nvic_irq_disable(dma_irq(handle));
    dma_interrupt_disable((uint32_t)handle->controller,
                          (dma_channel_enum)handle->channel,
                          DMA_INT_HTF | DMA_INT_FTF | DMA_INT_ERR);
    NVIC_ClearPendingIRQ(dma_irq(handle));
    handle->pending_events = 0U;
    if (config == NULL) {
        memset(&handle->interrupt, 0, sizeof(handle->interrupt));
    } else {
        handle->interrupt = *config;
        if (config->events & ADRV_DMA_EVENT_HALF) sources |= DMA_INT_HTF;
        if (config->events & ADRV_DMA_EVENT_COMPLETE) sources |= DMA_INT_FTF;
        if (config->events & ADRV_DMA_EVENT_ERROR) sources |= DMA_INT_ERR;
        dma_interrupt_enable((uint32_t)handle->controller,
                             (dma_channel_enum)handle->channel, sources);
        nvic_irq_enable(dma_irq(handle), config->priority, 0U);
    }
    dma_critical_exit(key);
    return A_STATUS_OK;
}

aStatus_t aDrvDmaGetProgress(aDrvDmaHandle_t *handle,
                            aDrvDmaProgress_t *progress)
{
    if (handle == NULL || progress == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->initialized || handle->length == 0U)
        return A_STATUS_NOT_READY;

    /* 每次只保护固定数量的寄存器读取，重试之间恢复原来的中断屏蔽状态。 */
    for (unsigned attempt = 0U; attempt < 8U; ++attempt) {
        const uint32_t key = dma_critical_enter();
        events_collect(handle);
        const uint32_t before = aDrvDmaCurLenGet(handle);
        __DMB();
        const uint32_t after = aDrvDmaCurLenGet(handle);
        const aBool_t stable = after <= before &&
            after <= handle->length &&
            dma_flag_get((uint32_t)handle->controller,
                         (dma_channel_enum)handle->channel,
                         DMA_FLAG_FTF) == RESET;
        const aStatus_t status = handle->error
                                     ? A_STATUS_ERROR : A_STATUS_OK;
        if (stable) {
            progress->remaining = after;
            progress->transferred = handle->circular
                ? handle->cycles * handle->length +
                    (after == 0U ? 0U : handle->length - after)
                : handle->length - after;
        }
        if (handle->pending_events != 0U)
            NVIC_SetPendingIRQ(dma_irq(handle));
        dma_critical_exit(key);
        if (stable) return status;
    }
    return A_STATUS_BUSY;
}

static void dma_dispatch(size_t index)
{
    const uint32_t key = dma_critical_enter();
    aDrvDmaHandle_t *handle = s_dma_owners[index];
    aDrvDmaCallback_t callback = NULL;
    void *argument = NULL;
    uint32_t events = 0U;

    if (handle != NULL && handle->initialized) {
        events_collect(handle);
        events = handle->pending_events;
        handle->pending_events = 0U;
        callback = handle->interrupt.callback;
        argument = handle->interrupt.argument;
    }
    dma_critical_exit(key);
    if (callback != NULL && events != 0U) callback(argument, events);
}

/* GD32E505 CL 的 DMA0 有七个通道，DMA1 有五个独立中断通道。 */
#define DMA_HANDLER(controller_, channel_, index_) \
    void DMA##controller_##_Channel##channel_##_IRQHandler(void) \
    { dma_dispatch(index_); }

DMA_HANDLER(0, 0, 0U)
DMA_HANDLER(0, 1, 1U)
DMA_HANDLER(0, 2, 2U)
DMA_HANDLER(0, 3, 3U)
DMA_HANDLER(0, 4, 4U)
DMA_HANDLER(0, 5, 5U)
DMA_HANDLER(0, 6, 6U)
DMA_HANDLER(1, 0, 7U)
DMA_HANDLER(1, 1, 8U)
DMA_HANDLER(1, 2, 9U)
DMA_HANDLER(1, 3, 10U)
DMA_HANDLER(1, 4, 11U)
