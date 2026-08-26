#ifndef ADRV_DMA_H
#define ADRV_DMA_H

#include "aDrv_basic.h"

typedef enum {
    ADRV_DMA_DIR_PERIPH_TO_MEMORY,
    ADRV_DMA_DIR_MEMORY_TO_PERIPH,
    ADRV_DMA_DIR_MEMORY_TO_MEMORY,
} aDrvDmaDirection_t;

typedef enum {
    ADRV_DMA_WIDTH_8,
    ADRV_DMA_WIDTH_16,
    ADRV_DMA_WIDTH_32,
} aDrvDmaWidth_t;

typedef enum {
    ADRV_DMA_PRIORITY_LOW,
    ADRV_DMA_PRIORITY_MEDIUM,
    ADRV_DMA_PRIORITY_HIGH,
    ADRV_DMA_PRIORITY_ULTRA,
} aDrvDmaPriority_t;

typedef struct {
    aDrvDmaChannel_t channel;
    aDrvDmaDirection_t direction;
    aDrvDmaWidth_t periphWidth;
    aDrvDmaWidth_t memoryWidth;
    aDrvDmaPriority_t priority;
    uint8_t periphIncrement;
    uint8_t memoryIncrement;
    uint8_t circular;
} aDrvDmaConfig_t;

typedef struct {
    uintptr_t controller;
    uint8_t channel;
    uint8_t initialized;
} aDrvDmaHandle_t;

void aDrvDmaConfigStructInit(aDrvDmaConfig_t *config);
void aDrvDmaHandleStructInit(aDrvDmaHandle_t *handle);
aStatus_t aDrvDmaInitStatic(const aDrvDmaConfig_t *config,
                            aDrvDmaHandle_t *handle);
aStatus_t aDrvDmaDeInitStatic(aDrvDmaHandle_t *handle);
aStatus_t aDrvDmaSrcBufferSet(aDrvDmaHandle_t *handle, const void *source);
aStatus_t aDrvDmaDstBufferSet(aDrvDmaHandle_t *handle, void *destination);
aStatus_t aDrvDmaDstBufferLen(aDrvDmaHandle_t *handle, uint32_t length);
aStatus_t aDrvDmaTransDisable(aDrvDmaHandle_t *handle);
aStatus_t aDrvDmaTransEnable(aDrvDmaHandle_t *handle);
uint32_t aDrvDmaCurLenGet(const aDrvDmaHandle_t *handle);

#endif
