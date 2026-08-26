#ifndef ADEV_USART_H
#define ADEV_USART_H

#include "aDrv_usart.h"
#include "aLib.h"

typedef struct {
    aDrvUsartConfig_t drv_config;
} aDevUsartConfig_t;

typedef struct {
    aDrvUsartHandle_t drv_handle;
} aDevUsartHandle_t;

void aDevUsartConfigStructInit(aDevUsartConfig_t *config);
void aDevUsartHandleStructInit(aDevUsartHandle_t *handle);
aStatus_t aDevUsartInit(const aDevUsartConfig_t *config,
                        aDevUsartHandle_t *handle);
aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle);
aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *buffer,
                       size_t buffer_size, aTimeout_t timeout);
aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *data,
                        size_t data_size, aTimeout_t timeout);
aStatus_t aDevUsartWaitTransmitComplete(aDevUsartHandle_t *handle,
                                         aTimeout_t timeout);
aStatus_t aDevUsartRegisterCallback(aDevUsartHandle_t *handle,
                                     const aDrvUsartExtiConfig_t *config);
aStatus_t aDevUsartUnregisterCallback(aDevUsartHandle_t *handle,
                                       aDrvUsartExti_t type);
void aDevUsartEnableInterrupt(aDevUsartHandle_t *handle);
void aDevUsartDisableInterrupt(aDevUsartHandle_t *handle);

#endif
