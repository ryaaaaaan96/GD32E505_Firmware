#ifndef ADEV_USART_H
#define ADEV_USART_H

#include "aDrv_usart.h"

typedef struct { aDrvUsartConfig_t drv_config; } aDevConfig_Usart_t;
typedef struct { aDrvUsartHandle_t drv_handle; } aDevHandle_Usart_t;

void aDevUsartConfigStructInit(aDevConfig_Usart_t *config);
void aDevUsartHandleStructInit(aDevHandle_Usart_t *handle);
aStatus_t aDevUsartInit(const aDevConfig_Usart_t *config,
                        aDevHandle_Usart_t *handle);
aStatus_t aDevUsartDeInit(aDevHandle_Usart_t *handle);
int32_t aDevUsartRead(aDevHandle_Usart_t *handle, void *buffer,
                      uint16_t size, uint32_t timeout_ms);
int32_t aDevUsartWrite(aDevHandle_Usart_t *handle, const void *buffer,
                       uint16_t size, uint32_t timeout_ms);
aStatus_t aDevUsartWaitTransmitComplete(aDevHandle_Usart_t *handle,
                                         uint32_t timeout_ms);
aStatus_t aDevUsartRegisterCallback(aDevHandle_Usart_t *handle,
                                     const aDrvUsartExtiConfig_t *config);
aStatus_t aDevUsartUnregisterCallback(aDevHandle_Usart_t *handle,
                                       aDrvUsartExti_t type);
void aDevUsartEnableInterrupt(aDevHandle_Usart_t *handle);
void aDevUsartDisableInterrupt(aDevHandle_Usart_t *handle);

#endif
