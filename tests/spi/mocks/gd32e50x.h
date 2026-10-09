#ifndef TEST_SPI_GD32E50X_H
#define TEST_SPI_GD32E50X_H
#include "../../modbus_demo/mocks/gd32e50x.h"
enum {
    SPI0 = 200, SPI1, SPI2, RCU_SPI0, RCU_SPI1, RCU_SPI2,
    SPI_PSC_2, SPI_PSC_4, SPI_PSC_8, SPI_PSC_16, SPI_PSC_32,
    SPI_PSC_64, SPI_PSC_128, SPI_PSC_256,
    SPI_CK_PL_HIGH_PH_2EDGE, SPI_CK_PL_HIGH_PH_1EDGE,
    SPI_CK_PL_LOW_PH_2EDGE, SPI_CK_PL_LOW_PH_1EDGE,
    SPI_MASTER, SPI_SLAVE, SPI_TRANSMODE_FULLDUPLEX,
    SPI_FRAMESIZE_16BIT, SPI_FRAMESIZE_8BIT, SPI_NSS_SOFT, SPI_NSS_HARD,
    SPI_ENDIAN_LSB, SPI_ENDIAN_MSB, SPI_FLAG_TBE, SPI_FLAG_RBNE,
    SPI_FLAG_CONFERR, SPI_FLAG_RXORERR, SPI_FLAG_TRANS
};
typedef struct {
    unsigned device_mode, trans_mode, frame_size, nss, endian;
    unsigned clock_polarity_phase, prescale;
} spi_parameter_struct;
void rcu_periph_clock_disable(rcu_periph_enum clock);
void spi_i2s_deinit(uint32_t instance);
void spi_struct_para_init(spi_parameter_struct *config);
void spi_init(uint32_t instance, const spi_parameter_struct *config);
void spi_nss_internal_high(uint32_t instance);
void spi_enable(uint32_t instance);
void spi_disable(uint32_t instance);
unsigned spi_i2s_flag_get(uint32_t instance, unsigned flag);
void spi_i2s_data_transmit(uint32_t instance, uint16_t value);
uint16_t spi_i2s_data_receive(uint32_t instance);
#endif
