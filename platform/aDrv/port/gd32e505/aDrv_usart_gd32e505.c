#include "aDrv_usart.h"
#include "aDrv_gd32e505_internal.h"
typedef struct { uint32_t instance; rcu_periph_enum clock; } usart_map_t;
static const usart_map_t s_usarts[ADRV_USART_COUNT] = {
    {USART0,RCU_USART0},{USART1,RCU_USART1},{USART2,RCU_USART2},
    {UART3,RCU_UART3},{UART4,RCU_UART4},{USART5,RCU_USART5}};
static aStatus_t wait_flag(uint32_t instance, usart_flag_enum flag, uint32_t timeout)
{
    const uint32_t start=aDrvGetTickMs();
    while(usart_flag_get(instance,flag)==RESET)
        if((uint32_t)(aDrvGetTickMs()-start)>=timeout) return A_STATUS_TIMEOUT;
    return A_STATUS_OK;
}
void aDrvUsartConfigStructInit(aDrvUsartConfig_t *c)
{ if(c){c->id=ADRV_USART_1;c->baud_rate=115200U;c->parity=ADRV_USART_PARITY_NONE;c->stop_bits=ADRV_USART_STOP_1;c->tx_pin=ADRV_PIN_NONE;c->rx_pin=ADRV_PIN_NONE;} }
void aDrvUsartHandleStructInit(aDrvUsartHandle_t *h)
{ if(h){h->instance=0U;h->baud_rate=0U;h->parity=ADRV_USART_PARITY_NONE;h->stop_bits=ADRV_USART_STOP_1;h->initialized=0U;} }
aStatus_t aDrvUsartInitStatic(const aDrvUsartConfig_t *c,aDrvUsartHandle_t *h)
{
    aDrvGd32Gpio_t tx,rx;
    if((c==NULL)||(h==NULL)||(c->id>=ADRV_USART_COUNT)||(c->baud_rate==0U)) return A_STATUS_INVALID_PARAM;
    if((aDrvGd32ResolvePin(c->tx_pin,&tx)!=A_STATUS_OK)||(aDrvGd32ResolvePin(c->rx_pin,&rx)!=A_STATUS_OK)) return A_STATUS_INVALID_PARAM;
    const usart_map_t *m=&s_usarts[c->id]; rcu_periph_clock_enable(RCU_AF); rcu_periph_clock_enable(m->clock);
    rcu_periph_clock_enable(tx.clock); rcu_periph_clock_enable(rx.clock);
    gpio_init(tx.port,GPIO_MODE_AF_PP,GPIO_OSPEED_50MHZ,tx.pin_mask);
    gpio_init(rx.port,GPIO_MODE_IN_FLOATING,GPIO_OSPEED_50MHZ,rx.pin_mask);
    usart_deinit(m->instance); usart_baudrate_set(m->instance,c->baud_rate);
    usart_word_length_set(m->instance,USART_WL_8BIT);
    usart_stop_bit_set(m->instance,c->stop_bits==ADRV_USART_STOP_2?USART_STB_2BIT:USART_STB_1BIT);
    usart_parity_config(m->instance,c->parity==ADRV_USART_PARITY_EVEN?USART_PM_EVEN:c->parity==ADRV_USART_PARITY_ODD?USART_PM_ODD:USART_PM_NONE);
    usart_transmit_config(m->instance,USART_TRANSMIT_ENABLE); usart_receive_config(m->instance,USART_RECEIVE_ENABLE); usart_enable(m->instance);
    h->instance=m->instance;h->baud_rate=c->baud_rate;h->parity=c->parity;h->stop_bits=c->stop_bits;h->initialized=1U;return A_STATUS_OK;
}
aStatus_t aDrvUsartDeInitStatic(aDrvUsartHandle_t *h)
{ if(h==NULL)return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;usart_disable((uint32_t)h->instance);aDrvUsartHandleStructInit(h);return A_STATUS_OK; }
int32_t aDrvUsartWriteByte(aDrvUsartHandle_t *h,const void *d)
{if((h==NULL)||(d==NULL))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;if(usart_flag_get((uint32_t)h->instance,USART_FLAG_TBE)==RESET)return 0;usart_data_transmit((uint32_t)h->instance,*(const uint8_t*)d);return 1;}
int32_t aDrvUsartReadByte(aDrvUsartHandle_t *h,void *d)
{if((h==NULL)||(d==NULL))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;if(usart_flag_get((uint32_t)h->instance,USART_FLAG_RBNE)==RESET)return 0;*(uint8_t*)d=(uint8_t)usart_data_receive((uint32_t)h->instance);return 1;}
aStatus_t aDrvUsartWaitTransmitComplete(aDrvUsartHandle_t *h,uint32_t t)
{if(h==NULL)return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;return wait_flag((uint32_t)h->instance,USART_FLAG_TC,t);}
aStatus_t aDrvUsartTransmit(aDrvUsartHandle_t*h,const uint8_t*d,size_t n,uint32_t t)
{if((h==NULL)||((d==NULL)&&(n!=0U)))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;for(size_t i=0;i<n;i++){aStatus_t s=wait_flag((uint32_t)h->instance,USART_FLAG_TBE,t);if(s)return s;usart_data_transmit((uint32_t)h->instance,d[i]);}return wait_flag((uint32_t)h->instance,USART_FLAG_TC,t);}
aStatus_t aDrvUsartReceive(aDrvUsartHandle_t*h,uint8_t*d,size_t n,uint32_t t)
{if((h==NULL)||((d==NULL)&&(n!=0U)))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;for(size_t i=0;i<n;i++){aStatus_t s=wait_flag((uint32_t)h->instance,USART_FLAG_RBNE,t);if(s)return s;d[i]=(uint8_t)usart_data_receive((uint32_t)h->instance);}return A_STATUS_OK;}
aStatus_t aDrvUsartRegisterCallback(aDrvUsartHandle_t*h,const aDrvUsartExtiConfig_t*c){if((h==NULL)||(c==NULL))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;return A_STATUS_UNSUPPORTED;}
aStatus_t aDrvUsartUnregisterCallback(aDrvUsartHandle_t*h,aDrvUsartExti_t t){(void)t;if(h==NULL)return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;return A_STATUS_UNSUPPORTED;}
void aDrvUsartEnableInterrupt(aDrvUsartHandle_t*h){(void)h;} void aDrvUsartDisableInterrupt(aDrvUsartHandle_t*h){(void)h;}
aStatus_t aDrvUsartSetBaudrate(aDrvUsartHandle_t*h,uint32_t b){if((h==NULL)||(b==0U))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;usart_baudrate_set((uint32_t)h->instance,b);h->baud_rate=b;return A_STATUS_OK;}
void aDrvUsartGetBaudrate(const aDrvUsartHandle_t*h,uint32_t*b){if(h&&b)*b=h->baud_rate;}
aStatus_t aDrvUsartSetStopbits(aDrvUsartHandle_t*h,aDrvUsartStopBits_t s){if((h==NULL)||(s>ADRV_USART_STOP_2))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;usart_stop_bit_set((uint32_t)h->instance,s==ADRV_USART_STOP_2?USART_STB_2BIT:USART_STB_1BIT);h->stop_bits=s;return A_STATUS_OK;}
void aDrvUsartGetStopbits(const aDrvUsartHandle_t*h,aDrvUsartStopBits_t*s){if(h&&s)*s=h->stop_bits;}
aStatus_t aDrvUsartSetParity(aDrvUsartHandle_t*h,aDrvUsartParity_t p){if((h==NULL)||(p>ADRV_USART_PARITY_ODD))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;usart_parity_config((uint32_t)h->instance,p==ADRV_USART_PARITY_EVEN?USART_PM_EVEN:p==ADRV_USART_PARITY_ODD?USART_PM_ODD:USART_PM_NONE);h->parity=p;return A_STATUS_OK;}
void aDrvUsartGetParity(const aDrvUsartHandle_t*h,aDrvUsartParity_t*p){if(h&&p)*p=h->parity;}
