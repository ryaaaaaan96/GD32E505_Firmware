#include "aDev_led_instance.h"
#include "gd32e50x.h"
#if ADEV_LED_DYNAMIC_ENABLE
#include "aOS.h"
#include <stdlib.h>
#endif

#include <assert.h>

/* 输入采样与输出锁存独立，覆盖外部电路拉低引脚的场景。 */
static uint32_t inputs[7];
static uint32_t outputs[7];
static uint32_t modes[7][16];
static unsigned hardware_calls;
static unsigned compensation_ready = SET;
static unsigned compensation_calls;
static unsigned gpio_calls;
static uint32_t last_speed;
uint32_t test_gpio_spd[7];

void gpio_compensation_config(uint32_t enable)
{
    assert(enable == GPIO_COMPENSATION_ENABLE);
    ++compensation_calls;
}
unsigned gpio_compensation_flag_get(void) { return compensation_ready; }


void rcu_periph_clock_enable(rcu_periph_enum clock)
{
    (void)clock;
    ++hardware_calls;
}

void gpio_pin_remap_config(uint32_t remap, unsigned enable)
{
    (void)remap;
    (void)enable;
    ++hardware_calls;
}

void gpio_init(uint32_t port, uint32_t mode, uint32_t speed, uint32_t pin)
{
    assert(port >= GPIOA && port <= GPIOG);
    last_speed = speed;
    ++gpio_calls;
    if (speed == GPIO_OSPEED_MAX) {
        assert(compensation_ready == SET);
        GPIOx_SPD(port) |= pin;
    }
    for (unsigned bit = 0; bit < 16; ++bit) {
        if (pin & (1U << bit)) modes[port - GPIOA][bit] = mode;
    }
    ++hardware_calls;
}

void gpio_bit_write(uint32_t port, uint32_t pin, unsigned value)
{
    assert(port >= GPIOA && port <= GPIOG);
    if (value == SET) outputs[port - GPIOA] |= pin;
    else outputs[port - GPIOA] &= ~pin;
    ++hardware_calls;
}

unsigned gpio_input_bit_get(uint32_t port, uint32_t pin)
{
    return (inputs[port - GPIOA] & pin) ? SET : RESET;
}

unsigned gpio_output_bit_get(uint32_t port, uint32_t pin)
{
    return (outputs[port - GPIOA] & pin) ? SET : RESET;
}

#if ADEV_LED_DYNAMIC_ENABLE
static unsigned allocations;
static aBool_t fail_alloc;

void *aOSAlloc(size_t size)
{
    void *memory;

    if (fail_alloc) return NULL;
    memory = malloc(size);
    assert(memory != NULL);
    ++allocations;
    return memory;
}

void aOSFree(void *memory)
{
    assert(memory != NULL && allocations != 0U);
    --allocations;
    free(memory);
}
#endif

static void check_state(aDevLedHandle_t *handle, aBool_t expected,
                        aDevLedActiveLevel_t active)
{
    aBool_t on = !expected;
    aDrvGpioLevel_t input;
    aDrvGpioLevel_t output;
    const aBool_t high = expected == (active == ADEV_LED_ACTIVE_HIGH);

    /* 强制输入与输出相反；LED 不能根据输入误判设置的亮灭状态。 */
    inputs[0] = high ? 0U : (1U << 8);
    assert(aDrvGpioRead(&handle->gpio, &input) == A_STATUS_OK);
    assert(aDrvGpioReadOutput(&handle->gpio, &output) == A_STATUS_OK);
    assert(output == (high ? ADRV_GPIO_HIGH : ADRV_GPIO_LOW));
    assert(input != output);
    assert(aDevLedGet(handle, &on) == A_STATUS_OK && on == expected);
}

static void check_operations(aDevLedHandle_t *handle,
                             aDevLedActiveLevel_t active, aBool_t initial)
{
    assert(modes[0][8] == GPIO_MODE_OUT_PP);
    check_state(handle, initial, active);
    assert(aDevLedToggle(handle) == A_STATUS_OK);
    check_state(handle, !initial, active);
    assert(aDevLedOn(handle) == A_STATUS_OK);
    check_state(handle, A_TRUE, active);
    assert(aDevLedOff(handle) == A_STATUS_OK);
    check_state(handle, A_FALSE, active);
    assert(aDevLedSet(handle, A_TRUE) == A_STATUS_OK);
    check_state(handle, A_TRUE, active);
}

static void check_deinit(aDevLedHandle_t *handle,
                         aDevLedActiveLevel_t active)
{
    aBool_t on = A_TRUE;

    assert(aDevLedDeInit(handle) == A_STATUS_OK);
    assert(modes[0][8] == GPIO_MODE_IN_FLOATING);
    assert(!!(outputs[0] & (1U << 8)) == (active == ADEV_LED_ACTIVE_LOW));
    assert(aDevLedGet(handle, &on) == A_STATUS_NOT_READY && on);
    assert(aDevLedSet(handle, A_TRUE) == A_STATUS_NOT_READY);
    assert(aDevLedToggle(handle) == A_STATUS_NOT_READY);
    assert(aDevLedDeInit(handle) == A_STATUS_NOT_READY);
}

static void test_lifecycle(void)
{
    aDevLedConfig_t config;
    aDevLedHandle_t instance;

    aDevLedConfigStructInit(&config);
    assert(config.pin == ADRV_PIN_NONE);
    assert(config.active_level == ADEV_LED_ACTIVE_HIGH);
    assert(config.initially_on == A_FALSE);
    config.pin = ADRV_PIN(ADRV_GPIO_PORT_A, 8);
    aDevLedHandleStructInit(&instance);
    assert(aDevLedDeInit(&instance) == A_STATUS_NOT_READY);

    for (unsigned active = 0; active < 2; ++active) {
        for (unsigned initial = 0; initial < 2; ++initial) {
            config.active_level = (aDevLedActiveLevel_t)active;
            config.initially_on = (aBool_t)initial;
#if ADEV_LED_STATIC_ENABLE
            assert(aDevLedInitStatic(&config, &instance) == A_STATUS_OK);
            check_operations(&instance, config.active_level,
                             config.initially_on);
#if ADEV_LED_DYNAMIC_ENABLE
            assert(aDevLedDestroy(&instance) == A_STATUS_INVALID_PARAM);
            assert(allocations == 0U);
#endif
            check_deinit(&instance, config.active_level);
#endif
#if ADEV_LED_DYNAMIC_ENABLE
            aDevLedHandle_t *dynamic = NULL;
            assert(aDevLedCreate(&config, &dynamic) == A_STATUS_OK);
            assert(allocations == 1U);
            check_operations(dynamic, config.active_level,
                             config.initially_on);
            check_deinit(dynamic, config.active_level);
            assert(aDevLedDestroy(dynamic) == A_STATUS_OK);
            assert(allocations == 0U);

            /* 也支持直接 Destroy 活动对象。 */
            assert(aDevLedCreate(&config, &dynamic) == A_STATUS_OK);
            assert(aDevLedDestroy(dynamic) == A_STATUS_OK);
            assert(allocations == 0U);
#endif
        }
    }
}

static void test_errors(void)
{
    aDevLedConfig_t config;
    aDevLedHandle_t unused;
    aBool_t on = A_TRUE;
    aDrvGpioLevel_t level = ADRV_GPIO_HIGH;
    const unsigned before = hardware_calls;

    aDevLedConfigStructInit(NULL);
    aDevLedHandleStructInit(NULL);
    aDevLedHandleStructInit(&unused);
    assert(aDevLedGet(NULL, &on) == A_STATUS_INVALID_PARAM && on);
    assert(aDevLedGet(&unused, NULL) == A_STATUS_INVALID_PARAM);
    assert(aDevLedGet(&unused, &on) == A_STATUS_NOT_READY && on);
    assert(aDevLedOn(NULL) == A_STATUS_INVALID_PARAM);
    assert(aDevLedOff(NULL) == A_STATUS_INVALID_PARAM);
    assert(aDevLedToggle(NULL) == A_STATUS_INVALID_PARAM);
    assert(aDevLedDeInit(NULL) == A_STATUS_INVALID_PARAM);
    assert(aDrvGpioReadOutput(NULL, &level) == A_STATUS_INVALID_PARAM);
    assert(aDrvGpioReadOutput(&unused.gpio, NULL) == A_STATUS_INVALID_PARAM);
    assert(aDrvGpioReadOutput(&unused.gpio, &level) == A_STATUS_NOT_READY);
    assert(level == ADRV_GPIO_HIGH);

    aDevLedConfigStructInit(&config);
#if ADEV_LED_STATIC_ENABLE
    assert(aDevLedInitStatic(NULL, &unused) == A_STATUS_INVALID_PARAM);
    assert(aDevLedInitStatic(&config, NULL) == A_STATUS_INVALID_PARAM);
#endif
#if ADEV_LED_DYNAMIC_ENABLE
    aDevLedHandle_t *handle = &unused;
    assert(aDevLedCreate(NULL, &handle) == A_STATUS_INVALID_PARAM);
    assert(handle == NULL);
    assert(aDevLedCreate(&config, NULL) == A_STATUS_INVALID_PARAM);
    assert(aDevLedDestroy(NULL) == A_STATUS_INVALID_PARAM);
    assert(aDevLedDestroy(&unused) == A_STATUS_INVALID_PARAM);
#endif

    /* NONE、超出端口范围的引脚、正负方向的非法枚举。 */
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        aDevLedConfigStructInit(&config);
        if (invalid == 1) config.pin = ADRV_PIN(7U, 0U);
        if (invalid >= 2) {
            config.pin = ADRV_PIN(ADRV_GPIO_PORT_A, 8);
            config.active_level = (aDevLedActiveLevel_t)
                (invalid == 2 ? -1 : 2);
        }
#if ADEV_LED_STATIC_ENABLE
        assert(aDevLedInitStatic(&config, &unused) == A_STATUS_INVALID_PARAM);
        assert(aDevLedOn(&unused) == A_STATUS_NOT_READY);
#endif
#if ADEV_LED_DYNAMIC_ENABLE
        handle = &unused;
        assert(aDevLedCreate(&config, &handle) == A_STATUS_INVALID_PARAM);
        assert(handle == NULL && allocations == 0U);
#endif
    }
    assert(hardware_calls == before);

#if ADEV_LED_DYNAMIC_ENABLE
    aDevLedConfigStructInit(&config);
    config.pin = ADRV_PIN(ADRV_GPIO_PORT_A, 8);
    fail_alloc = A_TRUE;
    handle = &unused;
    assert(aDevLedCreate(&config, &handle) == A_STATUS_NO_MEMORY);
    assert(handle == NULL && hardware_calls == before);
    fail_alloc = A_FALSE;

    /* 注入驱动错误，确认 Destroy 失败不提前释放对象。 */
    assert(aDevLedCreate(&config, &handle) == A_STATUS_OK);
    handle->gpio.pin = ADRV_PIN_NONE;
    assert(aDevLedDestroy(handle) == A_STATUS_INVALID_PARAM);
    assert(allocations == 1U);
    handle->gpio.pin = config.pin;
    assert(aDevLedDestroy(handle) == A_STATUS_OK);
    assert(allocations == 0U);
#endif
}

static void test_swd_protection(void)
{
    aDrvGpioConfig_t config;
    aDrvGpioHandle_t handle;

    aDrvGpioConfigStructInit(&config);
    aDrvGpioHandleStructInit(&handle);
    for (unsigned pin = 13; pin <= 14; ++pin) {
        config.pin = ADRV_PIN(ADRV_GPIO_PORT_A, pin);
        for (unsigned mode = ADRV_GPIO_INPUT;
             mode <= ADRV_GPIO_ANALOG; ++mode) {
            config.mode = (aDrvGpioMode_t)mode;
#if !ADRV_GPIO_SWD_PROTECT_DISABLE
            const unsigned before = hardware_calls;
            assert(aDrvGpioInit(&config, &handle) == A_STATUS_UNSUPPORTED);
            assert(hardware_calls == before);
            assert(!handle.initialized && handle.pin == ADRV_PIN_NONE);
#else
            assert(aDrvGpioInit(&config, &handle) == A_STATUS_OK);
            assert(handle.initialized && handle.pin == config.pin);
            assert(aDrvGpioDeInit(&handle) == A_STATUS_OK);
#endif
        }
    }
}

static void test_speeds(void)
{
    aDrvGpioConfig_t config;
    aDrvGpioHandle_t handle;
    unsigned calls;
    const uint32_t speeds[] = {
        GPIO_OSPEED_2MHZ, GPIO_OSPEED_10MHZ,
        GPIO_OSPEED_50MHZ, GPIO_OSPEED_MAX
    };

    aDrvGpioConfigStructInit(&config);
    aDrvGpioHandleStructInit(&handle);
    assert(config.speed == ADRV_GPIO_SPEED_HIGH);
    config.pin = ADRV_PIN(ADRV_GPIO_PORT_A, 8);
    config.mode = ADRV_GPIO_OUTPUT_PUSH_PULL;
    for (unsigned i = 0; i < 4; ++i) {
        config.speed = (aDrvGpioSpeed_t)i;
        assert(aDrvGpioInit(&config, &handle) == A_STATUS_OK);
        assert(last_speed == speeds[i]);
        assert(aDrvGpioDeInit(&handle) == A_STATUS_OK);
    }
    config.speed = ADRV_GPIO_SPEED_HIGH;
    assert(aDrvGpioInit(&config, &handle) == A_STATUS_OK);
    assert((GPIOx_SPD(GPIOA) & (1U << 8)) == 0U);
    assert(aDrvGpioDeInit(&handle) == A_STATUS_OK);
    calls = hardware_calls;
    config.speed = (aDrvGpioSpeed_t)-1;
    assert(aDrvGpioInit(&config, &handle) == A_STATUS_INVALID_PARAM);
    assert(hardware_calls == calls);
    config.speed = ADRV_GPIO_SPEED_MAX;
    compensation_ready = RESET;
    calls = gpio_calls;
    assert(aDrvGpioInit(&config, &handle) == A_STATUS_TIMEOUT);
    assert(gpio_calls == calls && !handle.initialized);
    assert(compensation_calls == 1U);
    config.mode = ADRV_GPIO_INPUT;
    assert(aDrvGpioInit(&config, &handle) == A_STATUS_OK);
    assert(compensation_calls == 1U);
    assert(aDrvGpioDeInit(&handle) == A_STATUS_OK);
    compensation_ready = SET;
}

int main(void)
{
    test_speeds();
    test_swd_protection();
    test_errors();
    test_lifecycle();
    return 0;
}
