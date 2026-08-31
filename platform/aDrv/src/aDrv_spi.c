#include "aDrv_spi.h"

#include "aDrv_internal.h"

typedef struct {
    uint32_t instance;
    rcu_periph_enum clock;
} spiMapping_t;

static const spiMapping_t spi_mappings[] = {
    {SPI0, RCU_SPI0},
    {SPI1, RCU_SPI1},
    {SPI2, RCU_SPI2},
};

static aStatus_t configure_pin(aDrvGpioPin_t pin, uint32_t mode)
{
    aDrvPrivateGpio_t gpio;

    if (aDrvResolvePin(pin, &gpio) != A_STATUS_OK) {
        return A_STATUS_INVALID_PARAM;
    }

    rcu_periph_clock_enable(gpio.clock);
    gpio_init(gpio.port, mode, GPIO_OSPEED_50MHZ, gpio.pin_mask);
    return A_STATUS_OK;
}

static uint32_t map_prescaler(uint32_t prescaler)
{
    if (prescaler >= 256U) {
        return SPI_PSC_256;
    }
    if (prescaler >= 128U) {
        return SPI_PSC_128;
    }
    if (prescaler >= 64U) {
        return SPI_PSC_64;
    }
    if (prescaler >= 32U) {
        return SPI_PSC_32;
    }
    if (prescaler >= 16U) {
        return SPI_PSC_16;
    }
    if (prescaler >= 8U) {
        return SPI_PSC_8;
    }
    if (prescaler >= 4U) {
        return SPI_PSC_4;
    }
    return SPI_PSC_2;
}

static uint32_t map_clock_mode(const aDrvSpiConfig_t *config)
{
    if (config->polarity == ADRV_SPI_POLARITY_HIGH) {
        return config->phase == ADRV_SPI_PHASE_2EDGE
                   ? SPI_CK_PL_HIGH_PH_2EDGE
                   : SPI_CK_PL_HIGH_PH_1EDGE;
    }

    return config->phase == ADRV_SPI_PHASE_2EDGE
               ? SPI_CK_PL_LOW_PH_2EDGE
               : SPI_CK_PL_LOW_PH_1EDGE;
}

void aDrvSpiConfigStructInit(aDrvSpiConfig_t *config)
{
    if (config == NULL) {
        return;
    }

    config->spiId = ADRV_SPI_1;
    config->mode = ADRV_SPI_MODE_MASTER;
    config->polarity = ADRV_SPI_POLARITY_LOW;
    config->phase = ADRV_SPI_PHASE_1EDGE;
    config->csMode = ADRV_SPI_CS_SOFT;
    config->bitOrder = ADRV_SPI_BITORDER_MSB;
    config->prescaler = 8U;
    config->dataBits = 8U;
    config->sckPin = ADRV_PIN_NONE;
    config->mosiPin = ADRV_PIN_NONE;
    config->misoPin = ADRV_PIN_NONE;
    config->csPin = ADRV_PIN_NONE;
}

void aDrvSpiHandleStructInit(aDrvSpiHandle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    handle->instance = 0U;
    handle->spiId = ADRV_SPI_1;
    handle->csPin = ADRV_PIN_NONE;
    handle->dataBytes = 1U;
    handle->softwareCs = A_FALSE;
    handle->initialized = A_FALSE;
}

aStatus_t aDrvSpiInitStatic(const aDrvSpiConfig_t *config,
                            aDrvSpiHandle_t *handle)
{
    const spiMapping_t *mapping;
    spi_parameter_struct parameters;

    if ((config == NULL) || (handle == NULL) ||
        ((size_t)config->spiId >= ADRV_ARRAY_COUNT(spi_mappings)) ||
        ((config->dataBits != 8U) && (config->dataBits != 16U)) ||
        (config->mode > ADRV_SPI_MODE_MASTER) ||
        (config->polarity > ADRV_SPI_POLARITY_HIGH) ||
        (config->phase > ADRV_SPI_PHASE_2EDGE) ||
        (config->csMode > ADRV_SPI_CS_HARD_OUTPUT) ||
        (config->bitOrder > ADRV_SPI_BITORDER_LSB)) {
        return A_STATUS_INVALID_PARAM;
    }

    if ((configure_pin(config->sckPin, GPIO_MODE_AF_PP) != A_STATUS_OK) ||
        (configure_pin(config->mosiPin, GPIO_MODE_AF_PP) != A_STATUS_OK) ||
        (configure_pin(config->misoPin, GPIO_MODE_IN_FLOATING) != A_STATUS_OK)) {
        return A_STATUS_INVALID_PARAM;
    }
    if ((config->csMode == ADRV_SPI_CS_SOFT) &&
        (configure_pin(config->csPin, GPIO_MODE_OUT_PP) != A_STATUS_OK)) {
        return A_STATUS_INVALID_PARAM;
    }

    mapping = &spi_mappings[config->spiId];
    rcu_periph_clock_enable(RCU_AF);
    rcu_periph_clock_enable(mapping->clock);
    spi_i2s_deinit(mapping->instance);

    spi_struct_para_init(&parameters);
    parameters.device_mode = config->mode == ADRV_SPI_MODE_MASTER
                                 ? SPI_MASTER
                                 : SPI_SLAVE;
    parameters.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    parameters.frame_size = config->dataBits == 16U ? SPI_FRAMESIZE_16BIT
                                                    : SPI_FRAMESIZE_8BIT;
    parameters.nss = config->csMode == ADRV_SPI_CS_SOFT ? SPI_NSS_SOFT
                                                        : SPI_NSS_HARD;
    parameters.endian = config->bitOrder == ADRV_SPI_BITORDER_LSB
                            ? SPI_ENDIAN_LSB
                            : SPI_ENDIAN_MSB;
    parameters.clock_polarity_phase = map_clock_mode(config);
    parameters.prescale = map_prescaler(config->prescaler);

    spi_init(mapping->instance, &parameters);
    spi_enable(mapping->instance);

    handle->instance = mapping->instance;
    handle->spiId = config->spiId;
    handle->csPin = config->csPin;
    handle->dataBytes = (uint8_t)(config->dataBits / 8U);
    handle->softwareCs = config->csMode == ADRV_SPI_CS_SOFT;
    handle->initialized = A_TRUE;

    if (handle->softwareCs != 0U) {
        return aDrvGpioWrite(handle->csPin, ADRV_GPIO_HIGH);
    }
    return A_STATUS_OK;
}

aStatus_t aDrvSpiDeInitStatic(aDrvSpiHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    spi_disable((uint32_t)handle->instance);
    spi_i2s_deinit((uint32_t)handle->instance);
    rcu_periph_clock_disable(spi_mappings[handle->spiId].clock);
    aDrvSpiHandleStructInit(handle);
    return A_STATUS_OK;
}

aStatus_t aDrvSpiTryWrite(aDrvSpiHandle_t *handle, const void *data)
{
    uint16_t value;

    if ((handle == NULL) || (data == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (spi_i2s_flag_get((uint32_t)handle->instance, SPI_FLAG_TBE) == RESET) {
        return A_STATUS_BUSY;
    }

    value = handle->dataBytes == 2U ? *(const uint16_t *)data
                                    : *(const uint8_t *)data;
    spi_i2s_data_transmit((uint32_t)handle->instance, value);
    return A_STATUS_OK;
}

aStatus_t aDrvSpiTryRead(aDrvSpiHandle_t *handle, void *data)
{
    uint16_t value;

    if ((handle == NULL) || (data == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (spi_i2s_flag_get((uint32_t)handle->instance, SPI_FLAG_RBNE) == RESET) {
        return A_STATUS_BUSY;
    }

    value = spi_i2s_data_receive((uint32_t)handle->instance);
    if (handle->dataBytes == 2U) {
        *(uint16_t *)data = value;
    } else {
        *(uint8_t *)data = (uint8_t)value;
    }
    return A_STATUS_OK;
}

aStatus_t aDrvSpiCsControl(aDrvSpiHandle_t *handle, uint8_t state)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (handle->softwareCs == 0U) {
        return A_STATUS_UNSUPPORTED;
    }

    return aDrvGpioWrite(handle->csPin,
                         state != 0U ? ADRV_GPIO_HIGH : ADRV_GPIO_LOW);
}
