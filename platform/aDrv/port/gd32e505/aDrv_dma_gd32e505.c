#include "aDrv_dma.h"
#include "aDrv_gd32e505_internal.h"
void aDrvDmaConfigStructInit(aDrvDmaConfig_t*c){if(c){c->channel=ADRV_DMA_CHANNEL_NONE;c->direction=ADRV_DMA_DIR_PERIPH_TO_MEMORY;c->periphWidth=ADRV_DMA_WIDTH_8;c->memoryWidth=ADRV_DMA_WIDTH_8;c->priority=ADRV_DMA_PRIORITY_LOW;c->periphIncrement=0U;c->memoryIncrement=1U;c->circular=0U;}}
void aDrvDmaHandleStructInit(aDrvDmaHandle_t*h){if(h){h->controller=0U;h->channel=0U;h->initialized=0U;}}
static uint32_t pw(aDrvDmaWidth_t w){return w==ADRV_DMA_WIDTH_32?DMA_PERIPHERAL_WIDTH_32BIT:w==ADRV_DMA_WIDTH_16?DMA_PERIPHERAL_WIDTH_16BIT:DMA_PERIPHERAL_WIDTH_8BIT;}
static uint32_t mw(aDrvDmaWidth_t w){return w==ADRV_DMA_WIDTH_32?DMA_MEMORY_WIDTH_32BIT:w==ADRV_DMA_WIDTH_16?DMA_MEMORY_WIDTH_16BIT:DMA_MEMORY_WIDTH_8BIT;}
aStatus_t aDrvDmaInitStatic(const aDrvDmaConfig_t*c,aDrvDmaHandle_t*h)
{
    aDrvDmaInfo_t i;if((c==NULL)||(h==NULL)||(aDrvParseDma(c->channel,&i)!=A_STATUS_OK))return A_STATUS_INVALID_PARAM;
    rcu_periph_clock_enable(i.controller==DMA0?RCU_DMA0:RCU_DMA1);dma_parameter_struct p;dma_struct_para_init(&p);
    p.periph_width=pw(c->periphWidth);p.memory_width=mw(c->memoryWidth);p.periph_inc=c->periphIncrement?DMA_PERIPH_INCREASE_ENABLE:DMA_PERIPH_INCREASE_DISABLE;p.memory_inc=c->memoryIncrement?DMA_MEMORY_INCREASE_ENABLE:DMA_MEMORY_INCREASE_DISABLE;p.direction=c->direction==ADRV_DMA_DIR_MEMORY_TO_PERIPH?DMA_MEMORY_TO_PERIPHERAL:DMA_PERIPHERAL_TO_MEMORY;
    static const uint32_t pr[]={DMA_PRIORITY_LOW,DMA_PRIORITY_MEDIUM,DMA_PRIORITY_HIGH,DMA_PRIORITY_ULTRA_HIGH};p.priority=pr[c->priority];
    dma_deinit((uint32_t)i.controller,(dma_channel_enum)i.channel);dma_init((uint32_t)i.controller,(dma_channel_enum)i.channel,&p);
    if(c->direction==ADRV_DMA_DIR_MEMORY_TO_MEMORY){
        dma_memory_to_memory_enable((uint32_t)i.controller,(dma_channel_enum)i.channel);
    }
    if(c->circular){
        dma_circulation_enable((uint32_t)i.controller,(dma_channel_enum)i.channel);
    }
    h->controller=i.controller;h->channel=i.channel;h->initialized=1U;return A_STATUS_OK;
}
aStatus_t aDrvDmaDeInitStatic(aDrvDmaHandle_t*h){if(h==NULL)return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;dma_deinit((uint32_t)h->controller,(dma_channel_enum)h->channel);aDrvDmaHandleStructInit(h);return A_STATUS_OK;}
aStatus_t aDrvDmaSrcBufferSet(aDrvDmaHandle_t*h,const void*s){if((h==NULL)||(s==NULL))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;dma_memory_address_config((uint32_t)h->controller,(dma_channel_enum)h->channel,(uint32_t)(uintptr_t)s);return A_STATUS_OK;}
aStatus_t aDrvDmaDstBufferSet(aDrvDmaHandle_t*h,void*d){if((h==NULL)||(d==NULL))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;dma_periph_address_config((uint32_t)h->controller,(dma_channel_enum)h->channel,(uint32_t)(uintptr_t)d);return A_STATUS_OK;}
aStatus_t aDrvDmaDstBufferLen(aDrvDmaHandle_t*h,uint32_t n){if((h==NULL)||(n==0U))return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;dma_transfer_number_config((uint32_t)h->controller,(dma_channel_enum)h->channel,n);return A_STATUS_OK;}
aStatus_t aDrvDmaTransDisable(aDrvDmaHandle_t*h){if(h==NULL)return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;dma_channel_disable((uint32_t)h->controller,(dma_channel_enum)h->channel);return A_STATUS_OK;}
aStatus_t aDrvDmaTransEnable(aDrvDmaHandle_t*h){if(h==NULL)return A_STATUS_INVALID_PARAM;if(!h->initialized)return A_STATUS_NOT_READY;dma_channel_enable((uint32_t)h->controller,(dma_channel_enum)h->channel);return A_STATUS_OK;}
uint32_t aDrvDmaCurLenGet(const aDrvDmaHandle_t*h){return(h&&h->initialized)?dma_transfer_number_get((uint32_t)h->controller,(dma_channel_enum)h->channel):0U;}
