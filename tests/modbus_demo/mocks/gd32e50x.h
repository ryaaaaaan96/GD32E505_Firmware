#ifndef TEST_GD32E50X_H
#define TEST_GD32E50X_H
#include <stdint.h>
typedef unsigned rcu_periph_enum;
typedef unsigned IRQn_Type;
enum {
    RESET, SET, DISABLE = 0, ENABLE = 1,
    GPIOA = 10, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG,
    RCU_GPIOA = 20, RCU_GPIOB, RCU_GPIOC, RCU_GPIOD, RCU_GPIOE,
    RCU_GPIOF, RCU_GPIOG, RCU_AF,
    USART0 = 30, USART1, USART2, UART3, UART4, USART5,
    RCU_USART0 = 40, RCU_USART1, RCU_USART2, RCU_UART3, RCU_UART4,
    RCU_USART5,
    USART0_IRQn = 50, USART1_IRQn, USART2_IRQn, UART3_IRQn,
    UART4_IRQn, USART5_IRQn,
    GPIO_USART2_PARTIAL_REMAP = 60, GPIO_USART2_FULL_REMAP,
    GPIO_SWJ_SWDPENABLE_REMAP,
    GPIO_MODE_IN_FLOATING = 70, GPIO_MODE_OUT_PP, GPIO_MODE_OUT_OD,
    GPIO_MODE_AF_PP, GPIO_MODE_AF_OD, GPIO_MODE_AIN, GPIO_OSPEED_50MHZ,
    USART_PM_EVEN = 80, USART_PM_ODD, USART_PM_NONE, USART_WL_8BIT,
    USART_STB_2BIT, USART_STB_1BIT, USART_TRANSMIT_ENABLE,
    USART_RECEIVE_ENABLE, USART_FLAG_TBE, USART_FLAG_RBNE, USART_FLAG_TC
};
void rcu_periph_clock_enable(rcu_periph_enum clock);
void gpio_pin_remap_config(uint32_t remap, unsigned enable);
void gpio_init(uint32_t port, uint32_t mode, uint32_t speed, uint32_t pin);
void gpio_bit_write(uint32_t port, uint32_t pin, unsigned value);
unsigned gpio_input_bit_get(uint32_t port, uint32_t pin);
void usart_deinit(uint32_t instance);
void usart_disable(uint32_t instance);
void usart_enable(uint32_t instance);
void usart_baudrate_set(uint32_t instance, uint32_t value);
void usart_word_length_set(uint32_t instance, uint32_t value);
void usart_stop_bit_set(uint32_t instance, uint32_t value);
void usart_parity_config(uint32_t instance, uint32_t value);
void usart_transmit_config(uint32_t instance, uint32_t value);
void usart_receive_config(uint32_t instance, uint32_t value);
void usart_data_transmit(uint32_t instance, uint32_t value);
uint16_t usart_data_receive(uint32_t instance);
unsigned usart_flag_get(uint32_t instance, uint32_t flag);
#endif
