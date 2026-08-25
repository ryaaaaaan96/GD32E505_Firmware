#ifndef ADEV_RS485_H
#define ADEV_RS485_H

#include "aDev_usart.h"
#include "aDrv_gpio.h"

typedef struct {
    aDevConfig_Usart_t usart_config;
    aDrvGpioPin_t de_pin;
    aDrvGpioPin_t re_pin;
    aDrvGpioLevel_t transmit_level;
    uint32_t timeout_ms;
} aDevConfig_RS485_t;

typedef struct {
    aDevHandle_Usart_t usart_handle;
    aDrvGpioPin_t de_pin;
    aDrvGpioPin_t re_pin;
    aDrvGpioLevel_t transmit_level;
    uint32_t timeout_ms;
    uint8_t initialized;
} aDevHandle_RS485_t;

typedef struct {
    uint32_t baudrate;
    aDrvUsartStopBits_t stopbits;
    aDrvUsartParity_t parity;
    uint32_t timeout_ms;
} aDevRS485Config_t;

void aDevRS485ConfigStructInit(aDevConfig_RS485_t *config);
void aDevRS485HandleStructInit(aDevHandle_RS485_t *handle);
aStatus_t aDevRS485Init(const aDevConfig_RS485_t *config,
                        aDevHandle_RS485_t *handle);
aStatus_t aDevRS485DeInit(aDevHandle_RS485_t *handle);
int32_t aDevRS485Read(aDevHandle_RS485_t *handle, void *buffer,
                      uint16_t size);
int32_t aDevRS485Write(aDevHandle_RS485_t *handle, const void *buffer,
                       uint16_t size);
aStatus_t aDevRS485SetConfig(aDevHandle_RS485_t *handle,
                             const aDevRS485Config_t *config);
aStatus_t aDevRS485GetConfig(const aDevHandle_RS485_t *handle,
                             aDevRS485Config_t *config);

#endif
