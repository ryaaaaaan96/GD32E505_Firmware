#include "aDrv_spi.h"
#include "aDrv_gd32e505_internal.h"
typedef struct{uint32_t instance;rcu_periph_enum clock;}spi_map_t;
static const spi_map_t s_spi[]={{SPI0,RCU_SPI0},{SPI1,RCU_SPI1},{SPI2,RCU_SPI2}};
static aStatus_t pin_cfg(aDrvGpioPin_t p,uint32_t mode){aDrvGd32Gpio_t g;if(aDrvGd32ResolvePin(p,&g)!=A_STATUS_OK)return A_STATUS_INVALID_PARAM;rcu_periph_clock_enable(g.clock);gpio_init(g.port,mode,GPIO_OSPEED_50MHZ,g.pin_mask);return A_STATUS_OK;}
static aStatus_t spi_wait(uint32_t i,uint32_t f,uint32_t t){uint32_t s=aDrvGetTickMs();while(spi_i2s_flag_get(i,f)==RESET)if((uint32_t)(aDrvGetTickMs()-s)>=t)return A_STATUS_TIMEOUT;return A_STATUS_OK;}
void aDrvSpiConfigStructInit(aDrvSpiConfig_t*c){if(c){c->spiId=ADRV_SPI_1;c->mode=ADRV_SPI_MODE_MASTER;c->polarity=ADRV_SPI_POLARITY_LOW;c->phase=ADRV_SPI_PHASE_1EDGE;c->csMode=ADRV_SPI_CS_SOFT;c->bitOrder=ADRV_SPI_BITORDER_MSB;c->prescaler=8U;c->dataBits=8U;c->sckPin=c->mosiPin=c->misoPin=c->csPin=ADRV_PIN_NONE;}}
void aDrvSpiHandleStructInit(aDrvSpiHandle_t*h){if(h){h->instance=0U;h->spiId=ADRV_SPI_1;h->csPin=ADRV_PIN_NONE;h->dataBytes=1U;h->softwareCs=0U;h->initialized=0U;}}
aStatus_t aDrvSpiInitStatic(const aDrvSpiConfig_t*c,aDrvSpiHandle_t*h)
{
    if((c==NULL)||(h==NULL)||(c->spiId>=ADRV_SPI_COUNT)||((c->dataBits!=8U)&&(c->dataBits!=16U)))return A_STATUS_INVALID_PARAM;
    if((pin_cfg(c->sckPin,GPIO_MODE_AF_PP)!=A_STATUS_OK)||(pin_cfg(c->mosiPin,GPIO_MODE_AF_PP)!=A_STATUS_OK)||(pin_cfg(c->misoPin,GPIO_MODE_IN_FLOATING)!=A_STATUS_OK))return A_STATUS_INVALID_PARAM;
    if((c->csMode==ADRV_SPI_CS_SOFT)&&(pin_cfg(c->csPin,GPIO_MODE_OUT_PP)!=A_STATUS_OK))return A_STATUS_INVALID_PARAM;
    const spi_map_t*m=&s_spi[c->spiId];rcu_periph_clock_enable(RCU_AF);rcu_periph_clock_enable(m->clock);spi_i2s_deinit(m->instance);
    spi_parameter_struct p;spi_struct_para_init(&p);p.device_mode=c->mode==ADRV_SPI_MODE_MASTER?SPI_MASTER:SPI_SLAVE;p.trans_mode=SPI_TRANSMODE_FULLDUPLEX;p.frame_size=c->dataBits==16U?SPI_FRAMESIZE_16BIT:SPI_FRAMESIZE_8BIT;p.nss=c->csMode==ADRV_SPI_CS_SOFT?SPI_NSS_SOFT:SPI_NSS_HARD;p.endian=c->bitOrder==ADRV_SPI_BITORDER_LSB?SPI_ENDIAN_LSB:SPI_ENDIAN_MSB;
    if(c->polarity==ADRV_SPI_POLARITY_HIGH)p.clock_polarity_phase=c->phase==ADRV_SPI_PHASE_2EDGE?SPI_CK_PL_HIGH_PH_2EDGE:SPI_CK_PL_HIGH_PH_1EDGE;else p.clock_polarity_phase=c->phase==ADRV_SPI_PHASE_2EDGE?SPI_CK_PL_LOW_PH_2EDGE:SPI_CK_PL_LOW_PH_1EDGE;
    p.prescale=c->prescaler>=256U?SPI_PSC_256:c->prescaler>=128U?SPI_PSC_128:c->prescaler>=64U?SPI_PSC_64:c->prescaler>=32U?SPI_PSC_32:c->prescaler>=16U?SPI_PSC_16:c->prescaler>=8U?SPI_PSC_8:c->prescaler>=4U?SPI_PSC_4:SPI_PSC_2;
    spi_init(m->instance,&p);spi_enable(m->instance);h->instance=m->instance;h->spiId=c->spiId;h->csPin=c->csPin;h->dataBytes=(uint8_t)(c->dataBits/8U);h->softwareCs=c->csMode==ADRV_SPI_CS_SOFT;h->initialized=1U;if(h->softwareCs)aDrvGpioWrite(h->csPin,ADRV_GPIO_HIGH);return A_STATUS_OK;
}
aStatus_t aDrvSpiDeInitStatic(aDrvSpiHandle_t*h){if(h==NULL)return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;spi_disable((uint32_t)h->instance);spi_i2s_deinit((uint32_t)h->instance);rcu_periph_clock_disable(s_spi[h->spiId].clock);aDrvSpiHandleStructInit(h);return A_STATUS_OK;}
int32_t aDrvSpiWriteByte(aDrvSpiHandle_t*h,const void*d){if((h==NULL)||(d==NULL))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;if(spi_i2s_flag_get((uint32_t)h->instance,SPI_FLAG_TBE)==RESET)return 0;spi_i2s_data_transmit((uint32_t)h->instance,h->dataBytes==2U?*(const uint16_t*)d:*(const uint8_t*)d);return h->dataBytes;}
int32_t aDrvSpiReadByte(aDrvSpiHandle_t*h,void*d){if((h==NULL)||(d==NULL))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;if(spi_i2s_flag_get((uint32_t)h->instance,SPI_FLAG_RBNE)==RESET)return 0;uint16_t v=spi_i2s_data_receive((uint32_t)h->instance);if(h->dataBytes==2U)*(uint16_t*)d=v;else*(uint8_t*)d=(uint8_t)v;return h->dataBytes;}
aStatus_t aDrvSpiCsControl(aDrvSpiHandle_t*h,uint8_t s){if(h==NULL)return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;if(!h->softwareCs)return A_STATUS_UNSUPPORTED;return aDrvGpioWrite(h->csPin,s?ADRV_GPIO_HIGH:ADRV_GPIO_LOW);}
aStatus_t aDrvSpiTransfer(aDrvSpiHandle_t*h,const uint8_t*tx,uint8_t*rx,size_t n,uint32_t t){if((h==NULL)||((tx==NULL)&&(rx==NULL))||(h->dataBytes!=1U))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;for(size_t x=0;x<n;x++){aStatus_t s=spi_wait((uint32_t)h->instance,SPI_FLAG_TBE,t);if(s)return s;spi_i2s_data_transmit((uint32_t)h->instance,tx?tx[x]:0xFFU);s=spi_wait((uint32_t)h->instance,SPI_FLAG_RBNE,t);if(s)return s;uint8_t v=(uint8_t)spi_i2s_data_receive((uint32_t)h->instance);if(rx)rx[x]=v;}return A_STATUS_OK;}
