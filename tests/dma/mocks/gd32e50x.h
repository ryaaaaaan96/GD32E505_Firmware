#ifndef TEST_DMA_GD32E50X_H
#define TEST_DMA_GD32E50X_H

#include "../../modbus_demo/mocks/gd32e50x.h"

typedef unsigned dma_channel_enum;
enum {
    DMA0 = 0, DMA1 = 1, RCU_DMA0 = 90, RCU_DMA1,
    DMA_CH0 = 0, DMA_CH1, DMA_CH2, DMA_CH3, DMA_CH4, DMA_CH5, DMA_CH6,
    DMA0_Channel0_IRQn = 0, DMA1_Channel0_IRQn = 7,
    DMA_PRIORITY_LOW = 0, DMA_PRIORITY_MEDIUM, DMA_PRIORITY_HIGH,
    DMA_PRIORITY_ULTRA_HIGH,
    DMA_PERIPHERAL_WIDTH_8BIT, DMA_PERIPHERAL_WIDTH_16BIT,
    DMA_PERIPHERAL_WIDTH_32BIT, DMA_MEMORY_WIDTH_8BIT,
    DMA_MEMORY_WIDTH_16BIT, DMA_MEMORY_WIDTH_32BIT,
    DMA_PERIPH_INCREASE_ENABLE, DMA_PERIPH_INCREASE_DISABLE,
    DMA_MEMORY_INCREASE_ENABLE, DMA_MEMORY_INCREASE_DISABLE,
    DMA_MEMORY_TO_PERIPHERAL, DMA_PERIPHERAL_TO_MEMORY,
    DMA_FLAG_FTF = 2, DMA_FLAG_HTF = 4, DMA_FLAG_ERR = 8,
    DMA_FLAG_G = 15, DMA_INT_FTF = 2, DMA_INT_HTF = 4, DMA_INT_ERR = 8,
    USART_TRANSMIT_DMA_ENABLE = 1, USART_TRANSMIT_DMA_DISABLE = 0,
    USART_RECEIVE_DMA_ENABLE = 1, USART_RECEIVE_DMA_DISABLE = 0,
};
typedef struct {
    uint32_t periph_width, memory_width, periph_inc, memory_inc;
    uint32_t direction, priority;
} dma_parameter_struct;
extern uint32_t test_usart_data;
#define USART_DATA(instance_) test_usart_data

uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);
void __DMB(void);
void NVIC_ClearPendingIRQ(IRQn_Type irq);
void NVIC_SetPendingIRQ(IRQn_Type irq);
void nvic_irq_disable(IRQn_Type irq);
void nvic_irq_enable(IRQn_Type irq, unsigned priority, unsigned subpriority);
void dma_struct_para_init(dma_parameter_struct *config);
void dma_deinit(uint32_t controller, dma_channel_enum channel);
void dma_init(uint32_t controller, dma_channel_enum channel,
              const dma_parameter_struct *config);
void dma_memory_to_memory_enable(uint32_t c, dma_channel_enum ch);
void dma_circulation_enable(uint32_t c, dma_channel_enum ch);
void dma_circulation_disable(uint32_t c, dma_channel_enum ch);
void dma_channel_disable(uint32_t c, dma_channel_enum ch);
void dma_channel_enable(uint32_t c, dma_channel_enum ch);
void dma_memory_address_config(uint32_t c, dma_channel_enum ch, uint32_t a);
void dma_periph_address_config(uint32_t c, dma_channel_enum ch, uint32_t a);
void dma_transfer_number_config(uint32_t c, dma_channel_enum ch, uint32_t n);
uint32_t dma_transfer_number_get(uint32_t c, dma_channel_enum ch);
unsigned dma_flag_get(uint32_t c, dma_channel_enum ch, uint32_t f);
void dma_flag_clear(uint32_t c, dma_channel_enum ch, uint32_t f);
void dma_interrupt_disable(uint32_t c, dma_channel_enum ch, uint32_t f);
void dma_interrupt_enable(uint32_t c, dma_channel_enum ch, uint32_t f);
void usart_dma_transmit_config(uint32_t instance, unsigned enabled);
void usart_dma_receive_config(uint32_t instance, unsigned enabled);

#endif
