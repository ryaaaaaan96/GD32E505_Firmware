/* 真实应用端口 + aModbus/nanoMODBUS + aBus，模拟串口线路。 */
#include "app_modbus.h"
#include "app_modbus_task.h"
#include "app_sig.h"
#include "aDev_usart.h"
#include "aOS.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void testAdvanceTime(uint32_t ms);
unsigned testAllocations(void);
static uint32_t counter;
APP_SIG_BIND(counter_binding, APP_BUS_COUNTER, counter);
static uint8_t input[600], output[600];
static size_t input_size, input_pos, output_size;
static unsigned opened, closed, waits;
static uint32_t input_at;
static aBool_t overflow, fail_task, close_busy;
static aDevUsartHandle_t *device;
static char dynamic_device;
#if APP_MODBUS_MASTER_ENABLE
static uint32_t response_value = 4321U;
static aBool_t response_enabled = A_TRUE, response_extra;
#endif

static uint16_t crc(const uint8_t *data, size_t size)
{
    uint16_t value = 0xffffU;
    for (size_t i = 0U; i < size; ++i) {
        value ^= data[i];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            value = value & 1U ? (value >> 1U) ^ 0xa001U : value >> 1U;
        }
    }
    return value;
}

static void feed(const uint8_t *data, size_t size)
{
    uint16_t check = crc(data, size);
    memcpy(input, data, size);
    input[size] = (uint8_t)check;
    input[size + 1U] = (uint8_t)(check >> 8U);
    input_size = size + 2U;
    input_pos = 0U;
    output_size = 0U;
    input_at = aOSGetUptimeMs();
}

static void check_config(const aDevUsartConfig_t *config)
{
    assert(config->drv_config.id == ADRV_USART_2);
    assert(config->drv_config.tx_pin == ADRV_PIN(ADRV_GPIO_PORT_C, 10));
    assert(config->drv_config.rx_pin == ADRV_PIN(ADRV_GPIO_PORT_C, 11));
    assert(config->drv_config.baud_rate == 115200U);
    assert(config->drv_config.parity == ADRV_USART_PARITY_NONE);
    assert(config->drv_config.stop_bits == ADRV_USART_STOP_1);
    assert(config->rs485.mode == ADEV_USART_RS485_GPIO_DE);
    assert(config->rs485.de_pin == ADRV_PIN(ADRV_GPIO_PORT_A, 15));
    assert(config->rs485.de_active_level == ADRV_GPIO_HIGH);
    assert(config->tx_buffer_size >= 256U);
    assert(config->rx_buffer_size >= 256U);
    assert(device == NULL);
    ++opened;
}

#if ADEV_USART_DYNAMIC_ENABLE
aStatus_t aDevUsartCreate(const aDevUsartConfig_t *config,
                          aDevUsartHandle_t **out)
{
    check_config(config);
    device = (aDevUsartHandle_t *)&dynamic_device;
    *out = device;
    return A_STATUS_OK;
}
aStatus_t aDevUsartDestroy(aDevUsartHandle_t *handle)
#else
aStatus_t aDevUsartInitStatic(const aDevUsartConfig_t *config,
                              aDevUsartHandle_t *handle)
{
    (void)dynamic_device;
    check_config(config);
    device = handle;
    return A_STATUS_OK;
}
aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle)
#endif
{
    assert(handle == device && device != NULL);
    if (close_busy) return A_STATUS_BUSY;
    device = NULL;
    ++closed;
    return A_STATUS_OK;
}

aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *data,
                       size_t size, aTimeout_t timeout)
{
    assert(handle == device && handle != NULL);
    assert(timeout.type == A_TIMEOUT_TYPE_RELATIVE);
    if (input_pos < input_size && aOSGetUptimeMs() < input_at) {
        uint32_t delay = input_at - aOSGetUptimeMs();
        testAdvanceTime(delay < timeout.milliseconds ?
                        delay : timeout.milliseconds);
    }
    if (input_pos < input_size && aOSGetUptimeMs() >= input_at) {
        size_t count = input_size - input_pos;
        if (count > size) count = size;
        if (count > 3U) count = 3U; /* 刻意拆成多次读取。 */
        memcpy(data, input + input_pos, count);
        input_pos += count;
        return (aSSize_t)count;
    }
    testAdvanceTime(timeout.milliseconds);
    return aOSFailWithStatus(timeout.milliseconds == 0U ?
                             A_STATUS_BUSY : A_STATUS_TIMEOUT);
}

aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *data,
                        size_t size, aTimeout_t timeout)
{
    assert(handle == device && timeout.milliseconds > 0U);
    assert(output_size + size <= sizeof(output));
#if APP_MODBUS_MASTER_ENABLE
    assert(size == 8U);
    assert(memcmp(data, (uint8_t[]){1, 3, 0, 2, 0, 2}, 6U) == 0);
    assert(crc(data, size) == 0U);
    if (response_enabled) {
        uint8_t response[] = {1, 3, 4,
            (uint8_t)(response_value >> 24U),
            (uint8_t)(response_value >> 16U),
            (uint8_t)(response_value >> 8U), (uint8_t)response_value};
        feed(response, sizeof(response));
        if (response_extra) input[input_size++] = 0U;
        input_at = aOSGetUptimeMs() + 3U;
    }
#endif
    memcpy(output + output_size, data, size);
    output_size += size;
    return (aSSize_t)size;
}

aStatus_t aDevUsartWaitTransmitComplete(aDevUsartHandle_t *handle,
                                        aTimeout_t timeout)
{
    assert(handle == device && timeout.milliseconds > 0U);
    ++waits;
    return A_STATUS_OK;
}
aBool_t aDevUsartHasRxOverflowed(const aDevUsartHandle_t *handle)
{
    assert(handle == device);
    return overflow;
}
void aDevUsartClearRxOverflow(aDevUsartHandle_t *handle)
{
    assert(handle == device);
    overflow = A_FALSE;
}
void aOSDelayMs(uint32_t ms) { testAdvanceTime(ms); }
aStatus_t aOSCreateTask(const aOSTaskConfig_t *task, aOSTaskHandle_t *out)
{
    assert(out == NULL && task->function != NULL);
    assert(strcmp(task->name, "modbus") == 0);
    assert(task->stack_bytes >= 3072U);
    return fail_task ? A_STATUS_NO_MEMORY : A_STATUS_OK;
}

static appBusMotor_t motor_get(void)
{
    appBusMotor_t motor;
    aBusGetIndexRequest_t get;
    aBusGetIndexRequestStructInit(&get);
    get.deviceID = APP_SIG_DEVICE_ID;
    get.sigIndex = APP_BUS_MOTOR;
    get.dst = &motor;
    get.size = sizeof(motor);
    assert(appSigGet(&get) == A_STATUS_OK);
    return motor;
}

#if !APP_MODBUS_MASTER_ENABLE
static void write_speed(uint8_t unit, uint32_t value)
{
    uint8_t request[] = {unit, 16, 0, 2, 0, 2, 4,
        (uint8_t)(value >> 24U), (uint8_t)(value >> 16U),
        (uint8_t)(value >> 8U), (uint8_t)value};
    feed(request, sizeof(request));
}

static void recover(void)
{
    assert(appModbusProcess() == A_STATUS_OK);
    assert(output_size == 0U);
}

static void server_tests(void)
{
    const uint8_t read[] = {1, 3, 0, 0, 0, 6};
    counter = 0x12345678U;
    feed(read, sizeof(read));
    assert(appModbusProcess() == A_STATUS_OK);
    assert(output_size == 17U && crc(output, output_size) == 0U);
    assert(memcmp(output, (uint8_t[]){1, 3, 12, 0x12, 0x34,
           0x56, 0x78, 0, 0, 0, 100, 0, 0, 0, 25}, 15U) == 0);
    assert(waits == 2U); /* 发送前检查和发送后 TC 确认。 */
    write_speed(1U, 4321U);
    assert(appModbusProcess() == A_STATUS_OK && output_size == 8U);
    assert(motor_get().speed == 4321U);
    assert(motor_get().temperature == 25);
    write_speed(1U, 6001U);
    assert(appModbusProcess() == A_STATUS_OK);
    assert(output_size == 5U && output[1] == 0x90 && output[2] == 3U);
    assert(motor_get().speed == 4321U);
    feed((uint8_t[]){1, 6, 0, 2, 0, 1}, 6U);
    assert(appModbusProcess() == A_STATUS_OK && output[2] == 2U);
    assert(motor_get().speed == 4321U);
    feed((uint8_t[]){1, 16, 0, 4, 0, 2, 4, 255, 255, 255, 236}, 11U);
    assert(appModbusProcess() == A_STATUS_OK);
    assert(motor_get().temperature == -20);
    write_speed(0U, 456U);
    assert(appModbusProcess() == A_STATUS_OK && output_size == 0U);
    assert(motor_get().speed == 456U);
    write_speed(2U, 789U);
    assert(appModbusProcess() == A_STATUS_OK && output_size == 0U);
    assert(motor_get().speed == 456U);
    write_speed(1U, 789U);
    input[input_size - 1U] ^= 1U;
    assert(appModbusProcess() != A_STATUS_OK && output_size == 0U);
    assert(motor_get().speed == 456U);
    recover();
    write_speed(1U, 789U);
    input[input_size++] = 0U;
    assert(appModbusProcess() != A_STATUS_OK && output_size == 0U);
    assert(motor_get().speed == 456U);
    recover();
    write_speed(1U, 789U);
    input_size = 3U;
    assert(appModbusProcess() != A_STATUS_OK && output_size == 0U);
    recover();
    write_speed(1U, 789U);
    overflow = A_TRUE;
    assert(appModbusProcess() != A_STATUS_OK && output_size == 0U);
    recover();
    write_speed(1U, 789U);
    assert(appModbusProcess() == A_STATUS_OK);
    assert(motor_get().speed == 789U);
}
#else
static void master_tests(void)
{
    assert(appModbusProcess() == A_STATUS_OK);
    assert(motor_get().speed == 4321U);
    assert(motor_get().temperature == 25);
    response_value = 6001U;
    assert(appModbusProcess() != A_STATUS_OK);
    assert(motor_get().speed == 4321U);
    response_enabled = A_FALSE;
    assert(appModbusProcess() == A_STATUS_TIMEOUT);
    assert(motor_get().speed == 4321U);
    response_enabled = A_TRUE;
    response_value = 321U;
    response_extra = A_TRUE;
    assert(appModbusProcess() != A_STATUS_OK);
    assert(motor_get().speed == 4321U);
    response_extra = A_FALSE;
    assert(appModbusProcess() == A_STATUS_OK);
    assert(motor_get().speed == 321U);
}
#endif

int main(void)
{
    unsigned baseline;
    assert(appModbusProcess() == A_STATUS_NOT_READY);
    assert(appModbusInit() == A_STATUS_NOT_READY);
    assert(opened == closed);
    assert(appSigInit() == A_STATUS_OK);
    baseline = testAllocations();
    assert(appModbusInit() == A_STATUS_OK);
    assert(appModbusInit() == A_STATUS_BUSY);
    fail_task = A_TRUE;
    assert(appModbusTaskInit() == A_STATUS_NO_MEMORY);
    assert(appModbusProcess() == A_STATUS_NOT_READY);
    assert(opened == closed && testAllocations() == baseline);
    fail_task = A_FALSE;
    assert(appModbusInit() == A_STATUS_OK);
    assert(appModbusTaskInit() == A_STATUS_OK);
#if APP_MODBUS_MASTER_ENABLE
    master_tests();
#else
    server_tests();
#endif
    close_busy = A_TRUE;
    assert(appModbusDeInit() == A_STATUS_BUSY);
    assert(appModbusProcess() == A_STATUS_NOT_READY);
    close_busy = A_FALSE;
    assert(appModbusDeInit() == A_STATUS_OK);
    assert(opened == closed && testAllocations() == baseline);
    puts("Modbus demo application/transport/data/lifecycle passed");
    return 0;
}
