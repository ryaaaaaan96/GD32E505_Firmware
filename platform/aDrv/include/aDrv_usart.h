#ifndef ADRV_USART_H
#define ADRV_USART_H
#include "aDrv.h"
#include "aDrv_gpio.h"
typedef enum { ADRV_USART_1, ADRV_USART_2, ADRV_USART_3, ADRV_USART_4,
    ADRV_USART_5, ADRV_USART_6, ADRV_USART_COUNT } aDrvUsartId_t;
typedef enum { ADRV_USART_PARITY_NONE, ADRV_USART_PARITY_EVEN, ADRV_USART_PARITY_ODD } aDrvUsartParity_t;
typedef enum { ADRV_USART_STOP_1, ADRV_USART_STOP_2 } aDrvUsartStopBits_t;
typedef struct { aDrvUsartId_t id; uint32_t baud_rate; aDrvUsartParity_t parity;
    aDrvUsartStopBits_t stop_bits; aDrvGpioPin_t tx_pin, rx_pin; } aDrvUsartConfig_t;
typedef struct { uintptr_t instance; uint32_t baud_rate; aDrvUsartParity_t parity;
    aDrvUsartStopBits_t stop_bits; uint8_t initialized; } aDrvUsartHandle_t;
typedef enum { ADRV_USART_EXTI_TXE, ADRV_USART_EXTI_RXNE, ADRV_USART_EXTI_TC,
    ADRV_USART_EXTI_IDLE, ADRV_USART_EXTI_ERROR, ADRV_USART_EXTI_MAX } aDrvUsartExti_t;
typedef struct { aDrvUsartExti_t trigger; uint32_t priority;
    aDrvInterruptCallback_t callback; void *argument; uint8_t enable; } aDrvUsartExtiConfig_t;
void aDrvUsartConfigStructInit(aDrvUsartConfig_t *config);
void aDrvUsartHandleStructInit(aDrvUsartHandle_t *handle);
aStatus_t aDrvUsartInitStatic(const aDrvUsartConfig_t *, aDrvUsartHandle_t *);
aStatus_t aDrvUsartDeInitStatic(aDrvUsartHandle_t *);
aStatus_t aDrvUsartTransmit(aDrvUsartHandle_t *, const uint8_t *, size_t, uint32_t);
aStatus_t aDrvUsartReceive(aDrvUsartHandle_t *, uint8_t *, size_t, uint32_t);
int32_t aDrvUsartWriteByte(aDrvUsartHandle_t *, const void *);
int32_t aDrvUsartReadByte(aDrvUsartHandle_t *, void *);
aStatus_t aDrvUsartWaitTransmitComplete(aDrvUsartHandle_t *, uint32_t);
aStatus_t aDrvUsartRegisterCallback(aDrvUsartHandle_t *, const aDrvUsartExtiConfig_t *);
aStatus_t aDrvUsartUnregisterCallback(aDrvUsartHandle_t *, aDrvUsartExti_t);
void aDrvUsartEnableInterrupt(aDrvUsartHandle_t *); void aDrvUsartDisableInterrupt(aDrvUsartHandle_t *);
aStatus_t aDrvUsartSetBaudrate(aDrvUsartHandle_t *, uint32_t);
void aDrvUsartGetBaudrate(const aDrvUsartHandle_t *, uint32_t *);
aStatus_t aDrvUsartSetStopbits(aDrvUsartHandle_t *, aDrvUsartStopBits_t);
void aDrvUsartGetStopbits(const aDrvUsartHandle_t *, aDrvUsartStopBits_t *);
aStatus_t aDrvUsartSetParity(aDrvUsartHandle_t *, aDrvUsartParity_t);
void aDrvUsartGetParity(const aDrvUsartHandle_t *, aDrvUsartParity_t *);
#endif
