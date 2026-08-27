#ifndef ADRV_USART_INTERNAL_H
#define ADRV_USART_INTERNAL_H

#include "aDrv_internal.h"
#include "aDrv_usart.h"

typedef struct {
    uint32_t instance;
    rcu_periph_enum clock;
    IRQn_Type irq;
} aDrvPrivateUsartMapping_t;

const aDrvPrivateUsartMapping_t *aDrvPrivateUsartMappingGet(
    aDrvUsartId_t id);
aDrvUsartHandle_t *aDrvPrivateUsartHandleGet(aDrvUsartId_t id);
void aDrvPrivateUsartHandleSet(aDrvUsartId_t id,
                               aDrvUsartHandle_t *handle);
aStatus_t aDrvPrivateUsartOwnerAcquire(aDrvUsartHandle_t *handle,
                                       aDrvUsartOwner_t owner);
void aDrvPrivateUsartOwnerRelease(aDrvUsartHandle_t *handle,
                                  aDrvUsartOwner_t owner);

#endif
