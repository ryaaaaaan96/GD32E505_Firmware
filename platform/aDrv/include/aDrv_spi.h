#ifndef ADRV_SPI_H
#define ADRV_SPI_H

#include "aDrv_gpio.h"

typedef enum {
    ADRV_SPI_1,
    ADRV_SPI_2,
    ADRV_SPI_3,
} aDrvSpiId_t;

typedef enum {
    ADRV_SPI_MODE_SLAVE,
    ADRV_SPI_MODE_MASTER,
} aDrvSpiMode_t;

typedef enum {
    ADRV_SPI_POLARITY_LOW,
    ADRV_SPI_POLARITY_HIGH,
} aDrvSpiClockPolarity_t;

typedef enum {
    ADRV_SPI_PHASE_1EDGE,
    ADRV_SPI_PHASE_2EDGE,
} aDrvSpiClockPhase_t;

typedef enum {
    ADRV_SPI_CS_SOFT,
    ADRV_SPI_CS_HARD_INPUT,
    ADRV_SPI_CS_HARD_OUTPUT,
} aDrvSpiCsMode_t;

typedef enum {
    ADRV_SPI_BITORDER_MSB,
    ADRV_SPI_BITORDER_LSB,
} aDrvSpiBitOrder_t;

typedef struct {
    aDrvSpiId_t spiId;
    aDrvSpiMode_t mode;
    aDrvSpiClockPolarity_t polarity;
    aDrvSpiClockPhase_t phase;
    aDrvSpiCsMode_t csMode;
    aDrvSpiBitOrder_t bitOrder;
    uint32_t prescaler;
    uint8_t dataBits;
    aDrvGpioPin_t sckPin;
    aDrvGpioPin_t mosiPin;
    aDrvGpioPin_t misoPin;
    aDrvGpioPin_t csPin;
} aDrvSpiConfig_t;

typedef struct {
    uintptr_t instance;
    aDrvSpiId_t spiId;
    aDrvGpioPin_t csPin;
    uint8_t dataBytes;
    aBool_t softwareCs;
    aBool_t initialized;
} aDrvSpiHandle_t;

void aDrvSpiConfigStructInit(aDrvSpiConfig_t *config);
void aDrvSpiHandleStructInit(aDrvSpiHandle_t *handle);
aStatus_t aDrvSpiInitStatic(const aDrvSpiConfig_t *config,
                            aDrvSpiHandle_t *handle);
aStatus_t aDrvSpiDeInitStatic(aDrvSpiHandle_t *handle);
aStatus_t aDrvSpiTryWrite(aDrvSpiHandle_t *handle, const void *data);
aStatus_t aDrvSpiTryRead(aDrvSpiHandle_t *handle, void *data);
aStatus_t aDrvSpiCsControl(aDrvSpiHandle_t *handle, uint8_t state);

#endif
