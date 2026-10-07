/* 验证真实 aDrv 初始化调用的路由选择及 DE 引脚释放顺序。 */
#include "aDrv_usart.h"
#include "aDrv_gpio.h"
#include "gd32e50x.h"
#include <assert.h>
#include <stdio.h>

static unsigned route, changes, stage;
static aBool_t swd_only;
void rcu_periph_clock_enable(rcu_periph_enum clock) { (void)clock; }
void gpio_pin_remap_config(uint32_t remap, unsigned enable)
{
    ++changes;
    if (remap == GPIO_SWJ_SWDPENABLE_REMAP) {
        assert(enable == ENABLE && stage == 2U);
        swd_only = A_TRUE;
        stage = 3U;
    } else if (enable == DISABLE) {
        assert(remap == GPIO_USART2_FULL_REMAP);
        route = 0U;
    } else route = remap;
}
void gpio_init(uint32_t port, uint32_t mode, uint32_t speed, uint32_t pin)
{
    (void)speed;
    if (port == GPIOA && pin == (1U << 15U)) {
        assert(stage == 1U && mode == GPIO_MODE_OUT_PP);
        stage = 2U;
    }
}
void gpio_bit_write(uint32_t port, uint32_t pin, unsigned value)
{
    assert(port == GPIOA && pin == (1U << 15U));
    assert(value == RESET && stage == 0U);
    stage = 1U;
}
void usart_deinit(uint32_t instance) { assert(instance == USART2); }
void usart_disable(uint32_t instance) { assert(instance == USART2); }
void usart_enable(uint32_t instance) { assert(instance == USART2); }
#define CONFIG_STUB(name) \
    void name(uint32_t instance, uint32_t value) \
    { assert(instance == USART2); (void)value; }
CONFIG_STUB(usart_baudrate_set)
CONFIG_STUB(usart_word_length_set)
CONFIG_STUB(usart_stop_bit_set)
CONFIG_STUB(usart_parity_config)
CONFIG_STUB(usart_transmit_config)
CONFIG_STUB(usart_receive_config)

int main(void)
{
    aDrvUsartConfig_t config;
    aDrvUsartHandle_t handle;
    aDrvGpioConfig_t gpio;
    aDrvGpioHandle_t de;
    unsigned before;

    aDrvUsartConfigStructInit(&config);
    aDrvUsartHandleStructInit(&handle);
    config.id = ADRV_USART_2;
    config.tx_pin = ADRV_PIN(ADRV_GPIO_PORT_C, 10);
    config.rx_pin = ADRV_PIN(ADRV_GPIO_PORT_C, 11);
    assert(aDrvUsartInitStatic(&config, &handle) == A_STATUS_OK);
    assert(route == GPIO_USART2_PARTIAL_REMAP);
    assert(aDrvUsartDeInitStatic(&handle) == A_STATUS_OK);
    config.tx_pin = ADRV_PIN(ADRV_GPIO_PORT_B, 10);
    config.rx_pin = ADRV_PIN(ADRV_GPIO_PORT_B, 11);
    assert(aDrvUsartInitStatic(&config, &handle) == A_STATUS_OK);
    assert(route == 0U);
    assert(aDrvUsartDeInitStatic(&handle) == A_STATUS_OK);
    config.tx_pin = ADRV_PIN(ADRV_GPIO_PORT_D, 8);
    config.rx_pin = ADRV_PIN(ADRV_GPIO_PORT_D, 9);
    assert(aDrvUsartInitStatic(&config, &handle) == A_STATUS_OK);
    assert(route == GPIO_USART2_FULL_REMAP);
    assert(aDrvUsartDeInitStatic(&handle) == A_STATUS_OK);
    config.rx_pin = ADRV_PIN(ADRV_GPIO_PORT_C, 11);
    before = changes;
    assert(aDrvUsartInitStatic(&config, &handle) == A_STATUS_INVALID_PARAM);
    assert(changes == before);
    aDrvGpioConfigStructInit(&gpio);
    gpio.pin = ADRV_PIN(ADRV_GPIO_PORT_A, 15);
    gpio.mode = ADRV_GPIO_OUTPUT_PUSH_PULL;
    gpio.initial_level = ADRV_GPIO_LOW;
    assert(aDrvGpioInit(&gpio, &de) == A_STATUS_OK);
    assert(swd_only && stage == 3U);
    assert(route == GPIO_USART2_FULL_REMAP);
    puts("USART2 routing and PA15 GPIO/SWD setup passed");
    return 0;
}
