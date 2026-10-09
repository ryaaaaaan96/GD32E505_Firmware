/* 配置拒绝不得触碰 GPIO/时钟；保留板上 SPI1 软件片选路径。 */
#include "aDrv_spi.h"
#include "gd32e50x.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned effects, cs_level;
void rcu_periph_clock_enable(rcu_periph_enum clock)
{ (void)clock; effects++; }
void rcu_periph_clock_disable(rcu_periph_enum clock)
{ (void)clock; effects++; }
void gpio_pin_remap_config(uint32_t remap, unsigned enable)
{ (void)remap; (void)enable; effects++; }
void gpio_init(uint32_t port, uint32_t mode, uint32_t speed, uint32_t pin)
{ (void)port; (void)mode; (void)speed; (void)pin; effects++; }
void gpio_bit_write(uint32_t port, uint32_t pin, unsigned value)
{
    assert(port == GPIOB && pin == (1U << 12U));
    cs_level = value;
    effects++;
}
void spi_i2s_deinit(uint32_t instance)
{ assert(instance == SPI1); effects++; }
void spi_struct_para_init(spi_parameter_struct *config)
{ memset(config, 0, sizeof(*config)); }
void spi_init(uint32_t instance, const spi_parameter_struct *config)
{
    assert(instance == SPI1);
    assert(config->device_mode == SPI_MASTER && config->nss == SPI_NSS_SOFT);
    assert(config->clock_polarity_phase == SPI_CK_PL_LOW_PH_1EDGE);
    assert(config->frame_size == SPI_FRAMESIZE_8BIT);
    assert(config->prescale == SPI_PSC_64);
    effects++;
}
void spi_nss_internal_high(uint32_t instance)
{ assert(instance == SPI1); effects++; }
void spi_enable(uint32_t instance)
{ assert(instance == SPI1); effects++; }
void spi_disable(uint32_t instance)
{ assert(instance == SPI1); effects++; }

int main(void)
{
    aDrvSpiConfig_t config;
    aDrvSpiHandle_t handle;
    aDrvSpiConfigStructInit(&config);
    aDrvSpiHandleStructInit(&handle);
    config.spiId = ADRV_SPI_1;
    config.sckPin = ADRV_PIN(ADRV_GPIO_PORT_B, 13);
    config.mosiPin = ADRV_PIN(ADRV_GPIO_PORT_B, 15);
    config.misoPin = ADRV_PIN(ADRV_GPIO_PORT_B, 14);
    config.csPin = ADRV_PIN(ADRV_GPIO_PORT_B, 12);
    config.prescaler = 64U;
    config.mode = ADRV_SPI_MODE_SLAVE;
    assert(aDrvSpiInitStatic(&config, &handle) == A_STATUS_UNSUPPORTED);
    config.mode = ADRV_SPI_MODE_MASTER;
    config.csMode = ADRV_SPI_CS_HARD_OUTPUT;
    assert(aDrvSpiInitStatic(&config, &handle) == A_STATUS_UNSUPPORTED);
    config.csMode = ADRV_SPI_CS_HARD_INPUT;
    assert(aDrvSpiInitStatic(&config, &handle) == A_STATUS_UNSUPPORTED);
    config.csMode = ADRV_SPI_CS_SOFT;
    config.csPin = (aDrvGpioPin_t)112U;
    assert(aDrvSpiInitStatic(&config, &handle) == A_STATUS_INVALID_PARAM);
    config.csPin = config.sckPin;
    assert(aDrvSpiInitStatic(&config, &handle) == A_STATUS_INVALID_PARAM);
    assert(effects == 0U && !handle.initialized);
    config.csPin = ADRV_PIN(ADRV_GPIO_PORT_B, 12);
    assert(aDrvSpiInitStatic(&config, &handle) == A_STATUS_OK);
    assert(effects != 0U && handle.initialized && cs_level == SET);
    assert(aDrvSpiCsControl(&handle, 0U) == A_STATUS_OK && cs_level == RESET);
    assert(aDrvSpiCsControl(&handle, 1U) == A_STATUS_OK && cs_level == SET);
    assert(aDrvSpiDeInitStatic(&handle) == A_STATUS_OK);
    puts("SPI1 configuration preflight and software CS passed");
    return 0;
}
