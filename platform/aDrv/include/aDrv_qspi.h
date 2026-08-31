#ifndef ADRV_QSPI_H
#define ADRV_QSPI_H

#include "aDrv_gpio.h"

#define ADRV_QSPI_FMODE_INDIRECT_WRITE 0U
#define ADRV_QSPI_FMODE_INDIRECT_READ  1U

#define ADRV_QSPI_INST_NONE    0U
#define ADRV_QSPI_INST_1_LINE  1U
#define ADRV_QSPI_INST_2_LINES 2U
#define ADRV_QSPI_INST_4_LINES 4U

#define ADRV_QSPI_ADDR_NONE    0U
#define ADRV_QSPI_ADDR_1_LINE  1U
#define ADRV_QSPI_ADDR_2_LINES 2U
#define ADRV_QSPI_ADDR_4_LINES 4U

#define ADRV_QSPI_DATA_NONE    0U
#define ADRV_QSPI_DATA_1_LINE  1U
#define ADRV_QSPI_DATA_2_LINES 2U
#define ADRV_QSPI_DATA_4_LINES 4U

typedef enum {
    ADRV_QSPI_1,
} aDrvQspiId_t;

typedef struct {
    aDrvQspiId_t qspiId;
    uint32_t clockPrescaler;
    uint32_t flashSize;
    aDrvGpioPin_t clkPin;
    aDrvGpioPin_t csPin;
    aDrvGpioPin_t io0Pin;
    aDrvGpioPin_t io1Pin;
    aDrvGpioPin_t io2Pin;
    aDrvGpioPin_t io3Pin;
} aDrvQspiConfig_t;

typedef struct {
    uintptr_t instance;
    aDrvQspiId_t qspiId;
    aDrvGpioPin_t csPin;
    uint32_t address;
    uint32_t transferLength;
    uint8_t functionalMode;
    aBool_t initialized;
} aDrvQspiHandle_t;

typedef struct {
    uint32_t Instruction;
    uint32_t InstructionMode;
    uint32_t Address;
    uint32_t AddressSize;
    uint32_t AddressMode;
    uint32_t DataMode;
    uint32_t NbData;
    uint32_t DummyCycles;
    uint32_t FunctionalMode;
} aDrvQspiCmd_t;

void aDrvQspiConfigStructInit(aDrvQspiConfig_t *config);
void aDrvQspiHandleStructInit(aDrvQspiHandle_t *handle);
aStatus_t aDrvQspiInitStatic(const aDrvQspiConfig_t *config,
                             aDrvQspiHandle_t *handle);
aStatus_t aDrvQspiDeInitStatic(aDrvQspiHandle_t *handle);
aStatus_t aDrvQspiCommand(aDrvQspiHandle_t *handle,
                          const aDrvQspiCmd_t *command);
aStatus_t aDrvQspiIsCommandComplete(const aDrvQspiHandle_t *handle,
                                    aBool_t *complete);
aStatus_t aDrvQspiTransmit(aDrvQspiHandle_t *handle, const uint8_t *data,
                           uint32_t length);
aStatus_t aDrvQspiReceive(aDrvQspiHandle_t *handle, uint8_t *data,
                          uint32_t length);
void aDrvQspiCsManual(aDrvQspiHandle_t *handle, aBool_t low);
void aDrvQspiCsAuto(aDrvQspiHandle_t *handle);

#endif
