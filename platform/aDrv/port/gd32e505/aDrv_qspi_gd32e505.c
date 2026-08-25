#include "aDrv_qspi.h"
#include "aDrv_gd32e505_internal.h"
#define GD32_SQPI_LOGIC_ADDRESS 0xB0000000UL
static aStatus_t configure_pin(aDrvGpioPin_t p){aDrvGd32Gpio_t g;if(aDrvGd32ResolvePin(p,&g)!=A_STATUS_OK)return A_STATUS_INVALID_PARAM;rcu_periph_clock_enable(g.clock);gpio_init(g.port,GPIO_MODE_AF_PP,GPIO_OSPEED_50MHZ,g.pin_mask);return A_STATUS_OK;}
static uint32_t sqpi_mode(const aDrvQspiCmd_t*c)
{
    if(c->InstructionMode==ADRV_QSPI_INST_1_LINE){if(c->AddressMode==ADRV_QSPI_ADDR_4_LINES||c->DataMode==ADRV_QSPI_DATA_4_LINES)return SQPI_MODE_SQQ;if(c->AddressMode==ADRV_QSPI_ADDR_2_LINES||c->DataMode==ADRV_QSPI_DATA_2_LINES)return SQPI_MODE_SDD;return SQPI_MODE_SSS;}return SQPI_MODE_QQQ;
}
static void configure_address_bits(uint32_t bits){SQPI_INIT=(SQPI_INIT&~(uint32_t)0x1F000000U)|((bits&0x1FU)<<24U);}
void aDrvQspiConfigStructInit(aDrvQspiConfig_t*c){if(c){c->qspiId=ADRV_QSPI_1;c->clockPrescaler=20U;c->flashSize=24U;c->clkPin=c->csPin=c->io0Pin=c->io1Pin=c->io2Pin=c->io3Pin=ADRV_PIN_NONE;}}
void aDrvQspiHandleStructInit(aDrvQspiHandle_t*h){if(h){h->instance=0U;h->qspiId=ADRV_QSPI_1;h->csPin=ADRV_PIN_NONE;h->address=0U;h->transferLength=0U;h->functionalMode=0U;h->initialized=0U;}}
aStatus_t aDrvQspiInitStatic(const aDrvQspiConfig_t*c,aDrvQspiHandle_t*h)
{
    if((c==NULL)||(h==NULL)||(c->qspiId>=ADRV_QSPI_MAX)||(c->clockPrescaler>63U))return A_STATUS_INVALID_PARAM;
    if((configure_pin(c->clkPin)!=A_STATUS_OK)||(configure_pin(c->csPin)!=A_STATUS_OK)||(configure_pin(c->io0Pin)!=A_STATUS_OK)||(configure_pin(c->io1Pin)!=A_STATUS_OK)||(configure_pin(c->io2Pin)!=A_STATUS_OK)||(configure_pin(c->io3Pin)!=A_STATUS_OK))return A_STATUS_INVALID_PARAM;
    rcu_periph_clock_enable(RCU_AF);rcu_periph_clock_enable(RCU_SQPI);sqpi_parameter_struct p;sqpi_struct_para_init(&p);p.addr_bit=0U;p.clk_div=c->clockPrescaler;p.cmd_bit=SQPI_CMDBIT_8_BITS;p.id_length=SQPI_ID_LENGTH_32_BITS;p.polarity=SQPI_SAMPLE_POLARITY_RISING;sqpi_init(&p);h->instance=SQPI;h->qspiId=c->qspiId;h->csPin=c->csPin;h->initialized=1U;return A_STATUS_OK;
}
aStatus_t aDrvQspiDeInitStatic(aDrvQspiHandle_t*h){if(h==NULL)return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;sqpi_deinit();rcu_periph_clock_disable(RCU_SQPI);aDrvQspiHandleStructInit(h);return A_STATUS_OK;}
aStatus_t aDrvQspiCommand(aDrvQspiHandle_t*h,const aDrvQspiCmd_t*c)
{
    if((h==NULL)||(c==NULL)||(c->DummyCycles>15U)||(c->AddressSize>31U)||
       ((c->AddressMode==ADRV_QSPI_ADDR_NONE)&&(c->Address!=0U))){
        return A_STATUS_INVALID_PARAM;
    }
    if(!h->initialized){
        return A_STATUS_NOT_READY;
    }
    uint32_t m=sqpi_mode(c);configure_address_bits(c->AddressMode==ADRV_QSPI_ADDR_NONE?0U:c->AddressSize);
    if(c->FunctionalMode==ADRV_QSPI_FMODE_INDIRECT_READ)sqpi_read_command_config(m,c->DummyCycles,c->Instruction);
    else if(c->FunctionalMode==ADRV_QSPI_FMODE_INDIRECT_WRITE){sqpi_write_command_config(m,c->DummyCycles,c->Instruction);if(c->DataMode==ADRV_QSPI_DATA_NONE){if(c->AddressMode==ADRV_QSPI_ADDR_NONE)sqpi_special_command();else{if(c->AddressSize<8U)return A_STATUS_UNSUPPORTED;configure_address_bits(c->AddressSize-8U);*(volatile uint8_t*)(GD32_SQPI_LOGIC_ADDRESS+(c->Address>>8U))=(uint8_t)c->Address;}}}else return A_STATUS_UNSUPPORTED;
    h->address=c->Address;h->transferLength=c->NbData;h->functionalMode=(uint8_t)c->FunctionalMode;return A_STATUS_OK;
}
aStatus_t aDrvQspiTransmit(aDrvQspiHandle_t*h,const uint8_t*d,uint32_t n){if((h==NULL)||(d==NULL)||((h->transferLength!=0U)&&(n>h->transferLength)))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;if(h->functionalMode!=ADRV_QSPI_FMODE_INDIRECT_WRITE)return A_STATUS_INVALID_PARAM;volatile uint8_t*dst=(volatile uint8_t*)(GD32_SQPI_LOGIC_ADDRESS+h->address);for(uint32_t i=0;i<n;i++)dst[i]=d[i];return A_STATUS_OK;}
aStatus_t aDrvQspiReceive(aDrvQspiHandle_t*h,uint8_t*d,uint32_t n){if((h==NULL)||(d==NULL)||((h->transferLength!=0U)&&(n>h->transferLength)))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;if(h->functionalMode!=ADRV_QSPI_FMODE_INDIRECT_READ)return A_STATUS_INVALID_PARAM;const volatile uint8_t*src=(const volatile uint8_t*)(GD32_SQPI_LOGIC_ADDRESS+h->address);for(uint32_t i=0;i<n;i++)d[i]=src[i];return A_STATUS_OK;}
void aDrvQspiCsManual(aDrvQspiHandle_t*h,bool low){(void)h;(void)low;}void aDrvQspiCsAuto(aDrvQspiHandle_t*h){(void)h;}
