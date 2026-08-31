#ifndef ADEV_RS485_H
#define ADEV_RS485_H

#include "aDev_usart.h"
#include "aDrv_gpio.h"

typedef struct {
    aDevUsartConfig_t usart_config;
    aDrvGpioPin_t de_pin;
    aDrvGpioPin_t re_pin;
    aDrvGpioLevel_t transmit_level;
} aDevRS485Config_t;

typedef struct {
    aDevUsartHandle_t usart_handle;
    aDrvGpioPin_t de_pin;
    aDrvGpioPin_t re_pin;
    aDrvGpioLevel_t transmit_level;
    aBool_t initialized;
} aDevRS485Handle_t;

typedef struct {
    uint32_t baudrate;
    aDrvUsartStopBits_t stopbits;
    aDrvUsartParity_t parity;
} aDevRS485LineConfig_t;

void aDevRS485ConfigStructInit(aDevRS485Config_t *config);
void aDevRS485HandleStructInit(aDevRS485Handle_t *handle);
aStatus_t aDevRS485Init(const aDevRS485Config_t *config,
                        aDevRS485Handle_t *handle);
aStatus_t aDevRS485DeInit(aDevRS485Handle_t *handle);
aSSize_t aDevRS485Read(aDevRS485Handle_t *handle, void *buffer,
                       size_t buffer_size, aTimeout_t timeout);
aSSize_t aDevRS485Write(aDevRS485Handle_t *handle, const void *data,
                        size_t data_size, aTimeout_t timeout);
aStatus_t aDevRS485SetLineConfig(aDevRS485Handle_t *handle,
                                 const aDevRS485LineConfig_t *config);
aStatus_t aDevRS485GetLineConfig(const aDevRS485Handle_t *handle,
                                 aDevRS485LineConfig_t *config);

#endif
