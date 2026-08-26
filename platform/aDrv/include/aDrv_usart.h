#ifndef ADRV_USART_H
#define ADRV_USART_H

#include "aDrv.h"
#include "aDrv_gpio.h"

#include <stdbool.h>

typedef enum {
    ADRV_USART_1,
    ADRV_USART_2,
    ADRV_USART_3,
    ADRV_USART_4,
    ADRV_USART_5,
    ADRV_USART_6,
} aDrvUsartId_t;

typedef enum {
    ADRV_USART_PARITY_NONE,
    ADRV_USART_PARITY_EVEN,
    ADRV_USART_PARITY_ODD,
} aDrvUsartParity_t;

typedef enum {
    ADRV_USART_STOP_1,
    ADRV_USART_STOP_2,
} aDrvUsartStopBits_t;

typedef struct {
    aDrvUsartId_t id;
    uint32_t baud_rate;
    aDrvUsartParity_t parity;
    aDrvUsartStopBits_t stop_bits;
    aDrvGpioPin_t tx_pin;
    aDrvGpioPin_t rx_pin;
} aDrvUsartConfig_t;

typedef struct {
    uintptr_t instance;
    uint32_t baud_rate;
    aDrvUsartParity_t parity;
    aDrvUsartStopBits_t stop_bits;
    uint8_t initialized;
} aDrvUsartHandle_t;

typedef enum {
    ADRV_USART_EXTI_TXE,
    ADRV_USART_EXTI_RXNE,
    ADRV_USART_EXTI_TC,
    ADRV_USART_EXTI_IDLE,
    ADRV_USART_EXTI_ERROR,
    ADRV_USART_EXTI_MAX,
} aDrvUsartExti_t;

typedef struct {
    aDrvUsartExti_t trigger;
    uint32_t priority;
    aDrvInterruptCallback_t callback;
    void *argument;
    uint8_t enable;
} aDrvUsartExtiConfig_t;

void aDrvUsartConfigStructInit(aDrvUsartConfig_t *config);
void aDrvUsartHandleStructInit(aDrvUsartHandle_t *handle);
aStatus_t aDrvUsartInitStatic(const aDrvUsartConfig_t *config,
                              aDrvUsartHandle_t *handle);
aStatus_t aDrvUsartDeInitStatic(aDrvUsartHandle_t *handle);
aStatus_t aDrvUsartTryWriteByte(aDrvUsartHandle_t *handle, uint8_t data);
aStatus_t aDrvUsartTryReadByte(aDrvUsartHandle_t *handle, uint8_t *data);
aStatus_t aDrvUsartIsTransmitComplete(
    const aDrvUsartHandle_t *handle, bool *complete);
aStatus_t aDrvUsartRegisterCallback(
    aDrvUsartHandle_t *handle, const aDrvUsartExtiConfig_t *config);
aStatus_t aDrvUsartUnregisterCallback(aDrvUsartHandle_t *handle,
                                      aDrvUsartExti_t trigger);
void aDrvUsartEnableInterrupt(aDrvUsartHandle_t *handle);
void aDrvUsartDisableInterrupt(aDrvUsartHandle_t *handle);
aStatus_t aDrvUsartSetBaudrate(aDrvUsartHandle_t *handle, uint32_t baud_rate);
void aDrvUsartGetBaudrate(const aDrvUsartHandle_t *handle,
                          uint32_t *baud_rate);
aStatus_t aDrvUsartSetStopbits(aDrvUsartHandle_t *handle,
                               aDrvUsartStopBits_t stop_bits);
void aDrvUsartGetStopbits(const aDrvUsartHandle_t *handle,
                          aDrvUsartStopBits_t *stop_bits);
aStatus_t aDrvUsartSetParity(aDrvUsartHandle_t *handle,
                             aDrvUsartParity_t parity);
void aDrvUsartGetParity(const aDrvUsartHandle_t *handle,
                        aDrvUsartParity_t *parity);

#endif
