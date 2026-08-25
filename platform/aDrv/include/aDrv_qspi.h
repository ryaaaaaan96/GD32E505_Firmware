#ifndef ADRV_QSPI_H
#define ADRV_QSPI_H
#include "aDrv_gpio.h"
#include <stdbool.h>
#define ADRV_QSPI_TIMEOUT_DEFAULT 5000U
#define ADRV_QSPI_FMODE_INDIRECT_WRITE 0U
#define ADRV_QSPI_FMODE_INDIRECT_READ 1U
#define ADRV_QSPI_INST_NONE 0U
#define ADRV_QSPI_INST_1_LINE 1U
#define ADRV_QSPI_INST_2_LINES 2U
#define ADRV_QSPI_INST_4_LINES 4U
#define ADRV_QSPI_ADDR_NONE 0U
#define ADRV_QSPI_ADDR_1_LINE 1U
#define ADRV_QSPI_ADDR_2_LINES 2U
#define ADRV_QSPI_ADDR_4_LINES 4U
#define ADRV_QSPI_DATA_NONE 0U
#define ADRV_QSPI_DATA_1_LINE 1U
#define ADRV_QSPI_DATA_2_LINES 2U
#define ADRV_QSPI_DATA_4_LINES 4U
typedef enum { ADRV_QSPI_1, ADRV_QSPI_MAX } aDrvQspiId_t;
typedef struct { aDrvQspiId_t qspiId; uint32_t clockPrescaler, flashSize;
    aDrvGpioPin_t clkPin, csPin, io0Pin, io1Pin, io2Pin, io3Pin; } aDrvQspiConfig_t;
typedef struct { uintptr_t instance; aDrvQspiId_t qspiId; aDrvGpioPin_t csPin;
    uint32_t address, transferLength; uint8_t functionalMode, initialized; } aDrvQspiHandle_t;
typedef struct { uint32_t Instruction, InstructionMode, Address, AddressSize,
    AddressMode, DataMode, NbData, DummyCycles, FunctionalMode; } aDrvQspiCmd_t;
void aDrvQspiConfigStructInit(aDrvQspiConfig_t *); void aDrvQspiHandleStructInit(aDrvQspiHandle_t *);
aStatus_t aDrvQspiInitStatic(const aDrvQspiConfig_t *, aDrvQspiHandle_t *);
aStatus_t aDrvQspiDeInitStatic(aDrvQspiHandle_t *);
aStatus_t aDrvQspiCommand(aDrvQspiHandle_t *, const aDrvQspiCmd_t *);
aStatus_t aDrvQspiTransmit(aDrvQspiHandle_t *, const uint8_t *, uint32_t);
aStatus_t aDrvQspiReceive(aDrvQspiHandle_t *, uint8_t *, uint32_t);
void aDrvQspiCsManual(aDrvQspiHandle_t *, bool); void aDrvQspiCsAuto(aDrvQspiHandle_t *);
#endif
