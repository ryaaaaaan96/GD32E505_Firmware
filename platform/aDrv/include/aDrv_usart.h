#ifndef ADRV_USART_H
#define ADRV_USART_H

#include "aDrv.h"
#include "aDrv_gpio.h"

typedef enum {
    ADRV_USART_0,
    ADRV_USART_1,
    ADRV_USART_2,
    ADRV_USART_3,
    ADRV_USART_4,
    ADRV_USART_5,
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

typedef enum {
    ADRV_USART_EXTI_TXE,
    ADRV_USART_EXTI_RXNE,
    ADRV_USART_EXTI_TC,
    ADRV_USART_EXTI_IDLE,
    ADRV_USART_EXTI_ERROR,
    ADRV_USART_EXTI_MAX,
} aDrvUsartExti_t;

typedef struct {
    aDrvInterruptCallback_t function;
    void *argument;
} aDrvUsartCallback_t;

typedef enum {
    ADRV_USART_OWNER_NONE = 0U,
    ADRV_USART_OWNER_INTERRUPT = 1U << 0,
    ADRV_USART_OWNER_ASYNC_TX = 1U << 1,
    ADRV_USART_OWNER_ASYNC_RX = 1U << 2,
} aDrvUsartOwner_t;

typedef struct {
    uintptr_t instance;
    uint32_t baud_rate;
    aDrvUsartParity_t parity;
    aDrvUsartStopBits_t stop_bits;
    aDrvUsartId_t id;
    aDrvUsartCallback_t callbacks[ADRV_USART_EXTI_MAX];
    aDrvUsartOwner_t owner;
    uint8_t irq_priority;
    aBool_t initialized;
} aDrvUsartHandle_t;

typedef struct {
    aDrvUsartExti_t trigger;
    uint32_t priority;
    aDrvInterruptCallback_t callback;
    void *argument;
    aBool_t enabled;
} aDrvUsartExtiConfig_t;

void aDrvUsartConfigStructInit(aDrvUsartConfig_t *config);
void aDrvUsartHandleStructInit(aDrvUsartHandle_t *handle);
aStatus_t aDrvUsartInitStatic(const aDrvUsartConfig_t *config,
                              aDrvUsartHandle_t *handle);
aStatus_t aDrvUsartDeInitStatic(aDrvUsartHandle_t *handle);
aStatus_t aDrvUsartTryWriteByte(aDrvUsartHandle_t *handle, uint8_t data);
aStatus_t aDrvUsartTryReadByte(aDrvUsartHandle_t *handle, uint8_t *data);
aStatus_t aDrvUsartIsTransmitComplete(
    const aDrvUsartHandle_t *handle, aBool_t *complete);
aStatus_t aDrvUsartRegisterCallback(
    aDrvUsartHandle_t *handle, const aDrvUsartExtiConfig_t *config);
aStatus_t aDrvUsartUnregisterCallback(aDrvUsartHandle_t *handle,
                                      aDrvUsartExti_t trigger);
aStatus_t aDrvUsartSetInterruptEnabled(aDrvUsartHandle_t *handle,
                                       aDrvUsartExti_t trigger,
                                       aBool_t enabled);
aBool_t aDrvUsartInterruptIsSupported(void);
void aDrvUsartEnableInterrupt(aDrvUsartHandle_t *handle);
void aDrvUsartDisableInterrupt(aDrvUsartHandle_t *handle);
aBool_t aDrvUsartAsyncTxIsSupported(const aDrvUsartHandle_t *handle);
aStatus_t aDrvUsartAsyncTxStart(aDrvUsartHandle_t *handle,
                                const void *data, size_t size,
                                size_t *started);
aStatus_t aDrvUsartAsyncTxGetRemaining(aDrvUsartHandle_t *handle,
                                       size_t *remaining);
aStatus_t aDrvUsartAsyncTxAbort(aDrvUsartHandle_t *handle);
aBool_t aDrvUsartAsyncRxIsSupported(const aDrvUsartHandle_t *handle);

/* Start one finite DMA reception. Use Stop() to obtain its received length. */
aStatus_t aDrvUsartAsyncRxStart(aDrvUsartHandle_t *handle,
                                void *buffer, size_t size);

/*
 * Start continuous circular DMA reception directly into buffer. The buffer is
 * owned by DMA until Stop()/Abort(); size is limited by the target DMA counter.
 */
aStatus_t aDrvUsartAsyncRxCircularStart(aDrvUsartHandle_t *handle,
                                        void *buffer, size_t size,
                                        uint8_t interrupt_priority);

/*
 * Return the cumulative byte count since CircularStart(). The unsigned count
 * may naturally wrap; callers obtain new bytes with unsigned subtraction.
 */
aStatus_t aDrvUsartAsyncRxGetReceivedCount(aDrvUsartHandle_t *handle,
                                           size_t *received);
aStatus_t aDrvUsartAsyncRxStop(aDrvUsartHandle_t *handle,
                               size_t *received);
aStatus_t aDrvUsartAsyncRxAbort(aDrvUsartHandle_t *handle);
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
