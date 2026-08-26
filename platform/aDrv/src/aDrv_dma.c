#include "aDrv_dma.h"

#include "aDrv_internal.h"

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
    {DMA1, DMA_CH5},
    {DMA1, DMA_CH6},
};

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
    config->periphIncrement = 0U;
    config->memoryIncrement = 1U;
    config->circular = 0U;
}

void aDrvDmaHandleStructInit(aDrvDmaHandle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    handle->controller = 0U;
    handle->channel = 0U;
    handle->initialized = 0U;
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

    if ((config == NULL) || (handle == NULL) ||
        ((size_t)config->priority >= ADRV_ARRAY_COUNT(priorities)) ||
        (config->direction > ADRV_DMA_DIR_MEMORY_TO_MEMORY) ||
        (config->periphWidth > ADRV_DMA_WIDTH_32) ||
        (config->memoryWidth > ADRV_DMA_WIDTH_32) ||
        (resolve_dma(config->channel, &mapping) != A_STATUS_OK)) {
        return A_STATUS_INVALID_PARAM;
    }

    rcu_periph_clock_enable(mapping.controller == DMA0 ? RCU_DMA0 : RCU_DMA1);

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
    handle->initialized = 1U;
    return A_STATUS_OK;
}

aStatus_t aDrvDmaDeInitStatic(aDrvDmaHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    dma_deinit((uint32_t)handle->controller,
               (dma_channel_enum)handle->channel);
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
    if ((handle == NULL) || (length == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    dma_transfer_number_config((uint32_t)handle->controller,
                               (dma_channel_enum)handle->channel, length);
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

    dma_channel_enable((uint32_t)handle->controller,
                       (dma_channel_enum)handle->channel);
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
