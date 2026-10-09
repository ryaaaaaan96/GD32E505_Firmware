#ifndef TEST_ADRV_INTERNAL_H
#define TEST_ADRV_INTERNAL_H
#include <stdint.h>
typedef unsigned rcu_periph_enum;
typedef unsigned IRQn_Type;
typedef unsigned usart5_interrupt_enum;
typedef unsigned usart5_interrupt_flag_enum;
typedef unsigned usart_interrupt_enum;
typedef unsigned usart_interrupt_flag_enum;
enum { RESET = 0, SET = 1 };
enum {
    USART_INT_PERR = 100, USART5_INT_PERR,
    USART_INT_FLAG_ERR_NERR, USART_INT_FLAG_ERR_FERR, USART_INT_FLAG_PERR,
    USART5_INT_FLAG_ERR_NERR, USART5_INT_FLAG_ERR_FERR, USART5_INT_FLAG_PERR,
    USART5_INT_ERR = 1,
    USART5_INT_FLAG_ERR_ORERR = 2,
    USART5_INT_FLAG_IDLE = 3,
    USART5_INT_FLAG_RBNE = 4,
    USART5_INT_FLAG_TBE = 5,
    USART5_INT_FLAG_TC = 6,
    USART5_INT_IDLE = 7,
    USART5_INT_RBNE = 8,
    USART5_INT_TBE = 9,
    USART5_INT_TC = 10,
    USART_INT_ERR = 11,
    USART_INT_FLAG_ERR_ORERR = 12,
    USART_INT_FLAG_IDLE = 13,
    USART_INT_FLAG_RBNE = 14,
    USART_INT_FLAG_TBE = 15,
    USART_INT_FLAG_TC = 16,
    USART_INT_IDLE = 17,
    USART_INT_RBNE = 18,
    USART_INT_TBE = 19,
    USART_INT_TC = 20
};
void nvic_irq_enable(IRQn_Type irq, unsigned priority, unsigned subpriority);
void nvic_irq_disable(IRQn_Type irq);
void NVIC_SetPendingIRQ(IRQn_Type irq);
void __DMB(void);
void usart_interrupt_enable(uint32_t instance, usart_interrupt_enum event);
void usart_interrupt_disable(uint32_t instance, usart_interrupt_enum event);
void usart5_interrupt_enable(uint32_t instance, usart5_interrupt_enum event);
void usart5_interrupt_disable(uint32_t instance, usart5_interrupt_enum event);
unsigned usart_interrupt_flag_get(uint32_t instance, usart_interrupt_flag_enum event);
unsigned usart5_interrupt_flag_get(uint32_t instance, usart5_interrupt_flag_enum event);
uint16_t usart_data_receive(uint32_t instance);
#endif
