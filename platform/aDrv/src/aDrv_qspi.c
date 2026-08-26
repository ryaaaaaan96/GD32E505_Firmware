#include "aDrv_qspi.h"

#include "aDrv_internal.h"

#define GD32_SQPI_LOGIC_ADDRESS 0xB0000000UL
#define GD32_SQPI_ADDRESS_MASK  0x1F000000U
#define GD32_SQPI_ADDRESS_SHIFT 24U

static aStatus_t configure_pin(aDrvGpioPin_t pin)
{
    aDrvPrivateGpio_t gpio;

    if (aDrvResolvePin(pin, &gpio) != A_STATUS_OK) {
        return A_STATUS_INVALID_PARAM;
    }

    rcu_periph_clock_enable(gpio.clock);
    gpio_init(gpio.port, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, gpio.pin_mask);
    return A_STATUS_OK;
}

static uint32_t command_mode(const aDrvQspiCmd_t *command)
{
    if (command->InstructionMode != ADRV_QSPI_INST_1_LINE) {
        return SQPI_MODE_QQQ;
    }
    if ((command->AddressMode == ADRV_QSPI_ADDR_4_LINES) ||
        (command->DataMode == ADRV_QSPI_DATA_4_LINES)) {
        return SQPI_MODE_SQQ;
    }
    if ((command->AddressMode == ADRV_QSPI_ADDR_2_LINES) ||
        (command->DataMode == ADRV_QSPI_DATA_2_LINES)) {
        return SQPI_MODE_SDD;
    }

    return SQPI_MODE_SSS;
}

static void configure_address_bits(uint32_t bits)
{
    SQPI_INIT = (SQPI_INIT & ~GD32_SQPI_ADDRESS_MASK) |
                ((bits & 0x1FU) << GD32_SQPI_ADDRESS_SHIFT);
}

static int32_t is_line_mode_valid(uint32_t mode, uint32_t none,
                                  uint32_t one, uint32_t two, uint32_t four)
{
    return (mode == none) || (mode == one) || (mode == two) ||
           (mode == four);
}

void aDrvQspiConfigStructInit(aDrvQspiConfig_t *config)
{
    if (config == NULL) {
        return;
    }

    config->qspiId = ADRV_QSPI_1;
    config->clockPrescaler = 20U;
    config->flashSize = 24U;
    config->clkPin = ADRV_PIN_NONE;
    config->csPin = ADRV_PIN_NONE;
    config->io0Pin = ADRV_PIN_NONE;
    config->io1Pin = ADRV_PIN_NONE;
    config->io2Pin = ADRV_PIN_NONE;
    config->io3Pin = ADRV_PIN_NONE;
}

void aDrvQspiHandleStructInit(aDrvQspiHandle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    handle->instance = 0U;
    handle->qspiId = ADRV_QSPI_1;
    handle->csPin = ADRV_PIN_NONE;
    handle->address = 0U;
    handle->transferLength = 0U;
    handle->functionalMode = 0U;
    handle->initialized = 0U;
}

aStatus_t aDrvQspiInitStatic(const aDrvQspiConfig_t *config,
                             aDrvQspiHandle_t *handle)
{
    sqpi_parameter_struct parameters;

    if ((config == NULL) || (handle == NULL) ||
        (config->qspiId != ADRV_QSPI_1) ||
        (config->clockPrescaler > 63U)) {
        return A_STATUS_INVALID_PARAM;
    }

    if ((configure_pin(config->clkPin) != A_STATUS_OK) ||
        (configure_pin(config->csPin) != A_STATUS_OK) ||
        (configure_pin(config->io0Pin) != A_STATUS_OK) ||
        (configure_pin(config->io1Pin) != A_STATUS_OK) ||
        (configure_pin(config->io2Pin) != A_STATUS_OK) ||
        (configure_pin(config->io3Pin) != A_STATUS_OK)) {
        return A_STATUS_INVALID_PARAM;
    }

    rcu_periph_clock_enable(RCU_AF);
    rcu_periph_clock_enable(RCU_SQPI);

    sqpi_struct_para_init(&parameters);
    parameters.addr_bit = 0U;
    parameters.clk_div = config->clockPrescaler;
    parameters.cmd_bit = SQPI_CMDBIT_8_BITS;
    parameters.id_length = SQPI_ID_LENGTH_32_BITS;
    parameters.polarity = SQPI_SAMPLE_POLARITY_RISING;
    sqpi_init(&parameters);

    handle->instance = SQPI;
    handle->qspiId = config->qspiId;
    handle->csPin = config->csPin;
    handle->initialized = 1U;
    return A_STATUS_OK;
}

aStatus_t aDrvQspiDeInitStatic(aDrvQspiHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    sqpi_deinit();
    rcu_periph_clock_disable(RCU_SQPI);
    aDrvQspiHandleStructInit(handle);
    return A_STATUS_OK;
}

aStatus_t aDrvQspiCommand(aDrvQspiHandle_t *handle,
                          const aDrvQspiCmd_t *command)
{
    uint32_t mode;

    if ((handle == NULL) || (command == NULL) ||
        (command->DummyCycles > 15U) || (command->AddressSize > 31U) ||
        ((command->AddressMode == ADRV_QSPI_ADDR_NONE) &&
         (command->Address != 0U)) ||
        !is_line_mode_valid(command->InstructionMode, ADRV_QSPI_INST_NONE,
                            ADRV_QSPI_INST_1_LINE, ADRV_QSPI_INST_2_LINES,
                            ADRV_QSPI_INST_4_LINES) ||
        !is_line_mode_valid(command->AddressMode, ADRV_QSPI_ADDR_NONE,
                            ADRV_QSPI_ADDR_1_LINE, ADRV_QSPI_ADDR_2_LINES,
                            ADRV_QSPI_ADDR_4_LINES) ||
        !is_line_mode_valid(command->DataMode, ADRV_QSPI_DATA_NONE,
                            ADRV_QSPI_DATA_1_LINE, ADRV_QSPI_DATA_2_LINES,
                            ADRV_QSPI_DATA_4_LINES)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    mode = command_mode(command);
    configure_address_bits(command->AddressMode == ADRV_QSPI_ADDR_NONE
                               ? 0U
                               : command->AddressSize);

    if (command->FunctionalMode == ADRV_QSPI_FMODE_INDIRECT_READ) {
        sqpi_read_command_config(mode, command->DummyCycles,
                                 command->Instruction);
    } else if (command->FunctionalMode == ADRV_QSPI_FMODE_INDIRECT_WRITE) {
        if ((command->DataMode == ADRV_QSPI_DATA_NONE) &&
            (command->AddressMode == ADRV_QSPI_ADDR_NONE) &&
            ((SQPI_WCMD & SQPI_WCMD_SCMD) != RESET)) {
            return A_STATUS_BUSY;
        }

        sqpi_write_command_config(mode, command->DummyCycles,
                                  command->Instruction);

        if (command->DataMode == ADRV_QSPI_DATA_NONE) {
            if (command->AddressMode == ADRV_QSPI_ADDR_NONE) {
                SQPI_WCMD |= SQPI_WCMD_SCMD;
            } else {
                volatile uint8_t *command_address;

                if (command->AddressSize < 8U) {
                    return A_STATUS_UNSUPPORTED;
                }
                configure_address_bits(command->AddressSize - 8U);
                command_address = (volatile uint8_t *)(
                    GD32_SQPI_LOGIC_ADDRESS + (command->Address >> 8U));
                *command_address = (uint8_t)command->Address;
            }
        }
    } else {
        return A_STATUS_UNSUPPORTED;
    }

    handle->address = command->Address;
    handle->transferLength = command->NbData;
    handle->functionalMode = (uint8_t)command->FunctionalMode;
    return A_STATUS_OK;
}

aStatus_t aDrvQspiIsCommandComplete(const aDrvQspiHandle_t *handle,
                                    bool *complete)
{
    if ((handle == NULL) || (complete == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    *complete = (SQPI_WCMD & SQPI_WCMD_SCMD) == RESET;
    return A_STATUS_OK;
}

aStatus_t aDrvQspiTransmit(aDrvQspiHandle_t *handle, const uint8_t *data,
                           uint32_t length)
{
    volatile uint8_t *destination;

    if ((handle == NULL) || (data == NULL) ||
        ((handle->transferLength != 0U) &&
         (length > handle->transferLength))) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (handle->functionalMode != ADRV_QSPI_FMODE_INDIRECT_WRITE) {
        return A_STATUS_INVALID_PARAM;
    }

    destination =
        (volatile uint8_t *)(GD32_SQPI_LOGIC_ADDRESS + handle->address);
    for (uint32_t index = 0U; index < length; ++index) {
        destination[index] = data[index];
    }
    return A_STATUS_OK;
}

aStatus_t aDrvQspiReceive(aDrvQspiHandle_t *handle, uint8_t *data,
                          uint32_t length)
{
    const volatile uint8_t *source;

    if ((handle == NULL) || (data == NULL) ||
        ((handle->transferLength != 0U) &&
         (length > handle->transferLength))) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (handle->functionalMode != ADRV_QSPI_FMODE_INDIRECT_READ) {
        return A_STATUS_INVALID_PARAM;
    }

    source = (const volatile uint8_t *)(GD32_SQPI_LOGIC_ADDRESS +
                                        handle->address);
    for (uint32_t index = 0U; index < length; ++index) {
        data[index] = source[index];
    }
    return A_STATUS_OK;
}

void aDrvQspiCsManual(aDrvQspiHandle_t *handle, bool low)
{
    (void)handle;
    (void)low;
}

void aDrvQspiCsAuto(aDrvQspiHandle_t *handle)
{
    (void)handle;
}
