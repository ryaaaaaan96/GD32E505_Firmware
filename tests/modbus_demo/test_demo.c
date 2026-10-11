/* 真实应用端口 + aModbus/nanoMODBUS + aBus，模拟串口线路。 */
#include "app_config.h"
#include "../../app/protocol/protocol.c"
#define testModbusInit(bus) modbusInit(&rs485_link, (bus))
#include "aPoll_instance.h"
#include "aModbus_instance.h"
static aStatus_t testModbusProcess(void)
{
    if (rs485_link.modbus == NULL) return A_STATUS_NOT_READY;
#if APP_MODBUS_MASTER_ENABLE
    aPollProcessResult_t result;
    aStatus_t status;
    if (rs485_link.poll == NULL) return A_STATUS_NOT_READY;
    for (;;) {
        status = aPollProcess(rs485_link.poll, &result);
        if (status != A_STATUS_OK) return status;
        if (result.type != APOLL_WORK_NONE) return result.execution.status;
        (void)aPollWait(rs485_link.poll, result.wait);
    }
#else
    return aModbusServerProcess(rs485_link.modbus, &rs485_link.settings->server);
#endif
}
#define testModbusDeInit() modbusDeInit(&rs485_link)
#include "IDU_sig_table.h"
#include "system_init.c"
#include "FAN_sig_table.h"
#include "rs485_device.h"
#if !AMODBUS_DYNAMIC_ENABLE
#include "aModbus_instance.h"
#endif
#include "aDev_usart.h"
#include "aOS.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>


void testAdvanceTime(uint32_t ms);
unsigned testAllocations(void);
void testFailAllocation(aBool_t fail);
static uint32_t counter;
ABUS_RAM_BIND_EXPORT(counter_binding,
                     PROTOCOL_BUS_INSTANCE_ID,
                     IDU_SIG_DEVICE_ID,
                     IDU_SIG_TEST_001,
                     counter);
static uint8_t input[600], output[600];
static size_t input_size, input_pos, output_size;
static unsigned opened, closed, waits;
static uint32_t input_at;
static aBool_t overflow, fail_task, close_busy;
static aBool_t fail_clock, fail_serial;
static aDevUsartHandle_t *device;
static char dynamic_device;
static aDevUsartRxByteCallback_t rx_callback;
static void *rx_context;
static uint32_t cycle_bias;
static aOSTaskConfig_t created_task;
static aBool_t task_test, run_immediately;
static void task_tests(void);
static jmp_buf task_done;
static size_t process_count, delay_count;
#if APP_MODBUS_MASTER_ENABLE
static size_t expected_poll;
#endif
static uint32_t task_delays[7];
static const aStatus_t task_results[] = {
    A_STATUS_OK, A_STATUS_OK, A_STATUS_TIMEOUT, A_STATUS_TIMEOUT,
    A_STATUS_OK, A_STATUS_OK, A_STATUS_OK
};

aStatus_t __real_aModbusPlanExecute(aModbusPlan_t *plan,
    const aModbusPlanRequest_t *request);
aStatus_t __real_aModbusServerProcess(aModbusHandle_t *handle,
    const aModbusServerProcessRequest_t *request);

static aStatus_t nextResult(void)
{
    if (process_count == 7U) longjmp(task_done, 1);
    return task_results[process_count++];
}

aStatus_t __wrap_aModbusPlanExecute(aModbusPlan_t *plan,
    const aModbusPlanRequest_t *request)
{
    aStatus_t status;
#if APP_MODBUS_MASTER_ENABLE
    assert(request->frame_index == expected_poll);
    assert(plan->config.unit_id ==
           FAN_modbus_master_config.service.modbus.unit_id);
    assert(plan->config.timeout.milliseconds ==
           FAN_modbus_master_config.service.request_timeout.milliseconds);
#endif
    if (task_test) {
        *request->attempted = A_TRUE;
        status = nextResult();
    } else {
        status = __real_aModbusPlanExecute(plan, request);
    }
#if APP_MODBUS_MASTER_ENABLE
    if (*request->attempted) {
        expected_poll = (expected_poll + 1U) % aModbusPlanFrameCount(plan);
    }
#endif
    return status;
}

aStatus_t __wrap_aModbusServerProcess(aModbusHandle_t *handle,
    const aModbusServerProcessRequest_t *request)
{
    if (task_test) return nextResult();
    return __real_aModbusServerProcess(handle, request);
}

static void deliver(void)
{
    if (overflow) {
        rx_callback(rx_context, 0U, A_STATUS_ERROR);
        overflow = A_FALSE;
    }
    if (aOSGetUptimeMs() >= input_at) {
        while (input_pos < input_size)
            rx_callback(rx_context, input[input_pos++], A_STATUS_OK);
    }
}
aStatus_t aDrvCycleCounterEnable(void)
{ return fail_clock ? A_STATUS_UNSUPPORTED : A_STATUS_OK; }
uint32_t aDrvGetCoreClockHz(void) { return 180000000U; }
uint32_t aDrvCycleCounterRead(void)
{ return aOSGetUptimeMs() * 180000U + cycle_bias; }
void aOSCriticalEnter(void) {}
void aOSCriticalExit(void) {}
void aDevUsartConfigStructInit(aDevUsartConfig_t *config)
{
    memset(config, 0, sizeof(*config));
}
void aDevUsartClearRxError(aDevUsartHandle_t *handle) { (void)handle; }

#if APP_MODBUS_MASTER_ENABLE
static uint32_t response_value = 4321U;
static aBool_t response_enabled = A_TRUE, response_extra;
static uint16_t last_address, last_quantity;
static aBool_t check_reentry;

/* 独立模拟远端寄存器，数值刻意不同于本地默认值。 */
static uint16_t remote_word(uint16_t address)
{
    switch (address) {
    case 2U: return (uint16_t)(response_value >> 16U);
    case 3U: return (uint16_t)response_value;
    case 4U: return UINT16_MAX;
    case 5U: return (uint16_t)-123;
    case 32U: return 1U;
    case 34U: return 0U;
    case 35U: return 2345U;
    case 36U: return 2U;
    case 38U: return UINT16_MAX;
    case 39U: return (uint16_t)-42;
    case 40U: return 0x1122U;
    case 41U: return 0x3344U;
    case 42U: return 0x5566U;
    case 43U: return 0x7788U;
    case 44U: return 0U;
    case 45U: return 567U;
    case 46U: return UINT16_MAX;
    case 47U: return (uint16_t)-89;
    default:
        /* 读到空洞或 TEST_031～050 即失败，不能静默扩宽采集范围。 */
        assert(address >= 64U && address <= 109U);
        return address % 2U == 0U ? 0U :
               (uint16_t)(1008U + (address - 64U) / 2U);
    }
}
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
    assert(config->drv_config.interrupt_priority == 6U);
    assert(config->rs485.mode == ADEV_USART_RS485_GPIO_DE);
    assert(config->rs485.de_pin == ADRV_PIN(ADRV_GPIO_PORT_A, 15));
    assert(config->rs485.de_active_level == ADRV_GPIO_HIGH);
    assert(config->tx_buffer_size >= 256U);
    assert((config->mode & ADEV_USART_RX_MASK) ==
           ADEV_USART_RX_INTERRUPT_CALLBACK);
    assert(config->rx_byte_callback != NULL);
    rx_callback = config->rx_byte_callback;
    rx_context = config->rx_byte_context;
    assert(device == NULL);
    ++opened;
}

#if ADEV_USART_DYNAMIC_ENABLE
aStatus_t aDevUsartCreate(const aDevUsartConfig_t *config,
                          aDevUsartHandle_t **out)
{
    if (fail_serial) return A_STATUS_ERROR;
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
    if (fail_serial) return A_STATUS_ERROR;
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
    const uint8_t *frame = data;
    assert((frame[1] == 3U || frame[1] == 6U) && size == 8U);
    if (frame[1] == 6U) {
        feed(frame, 6U);
        input_at = aOSGetUptimeMs() + 3U;
        memcpy(output, data, size);
        output_size = size;
        return (aSSize_t)size;
    }
    assert(frame[0] == 1U);
    last_address = (uint16_t)((uint16_t)frame[2] << 8U | frame[3]);
    last_quantity = (uint16_t)((uint16_t)frame[4] << 8U | frame[5]);
    assert(last_quantity > 0U && last_quantity <= 125U);
    assert(crc(data, size) == 0U);
    if (check_reentry) {
        assert(testModbusProcess() == A_STATUS_BUSY);
    }
    if (response_enabled) {
        uint8_t response[253] = {1U, 3U, (uint8_t)(last_quantity * 2U)};
        for (uint16_t i = 0U; i < last_quantity; i++) {
            uint16_t value = remote_word((uint16_t)(last_address + i));
            response[3U + i * 2U] = (uint8_t)(value >> 8U);
            response[4U + i * 2U] = (uint8_t)value;
        }
        feed(response, 3U + last_quantity * 2U);
        if (response_extra && last_address == 2U) input[input_size++] = 0U;
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

void aOSDelayMs(uint32_t ms)
{
    if (task_test) {
        assert(delay_count < 7U);
        task_delays[delay_count++] = ms;
        testAdvanceTime(ms);
        return;
    }
    testAdvanceTime(ms);
    deliver();
}
aStatus_t aOSCreateTask(const aOSTaskConfig_t *task, aOSTaskHandle_t *out)
{
    assert(out == NULL && task->function != NULL);
    assert(strcmp(task->name, "modbus") == 0);
    assert(task->stack_bytes >= 3072U);
    /* 创建任务前串口与接收者必须已就绪。 */
    assert(device != NULL && rx_callback != NULL && rx_context != NULL);
    created_task = *task;
#if APP_MODBUS_MASTER_ENABLE
    expected_poll = 0U;
#endif
    if (fail_task) return A_STATUS_NO_MEMORY;
    if (run_immediately) task_tests();
    return A_STATUS_OK;
}

static FANTest_t motor_get(void)
{
    FANTest_t motor;
    aBusGetIndexRequest_t get;
    aBusGetIndexRequestStructInit(&get);
    get.deviceID = FAN_SIG_DEVICE_ID;
    get.sigIndex = FAN_SIG_TEST_001;
    get.dst = &motor;
    get.size = sizeof(motor);
    assert(dataBusGet(&get) == A_STATUS_OK);
    return motor;
}

#if !APP_MODBUS_MASTER_ENABLE
static uint32_t target_get(void)
{
    uint32_t value;
    const aBusGetIndexRequest_t get = {
        .deviceID = FAN_SIG_DEVICE_ID,
        .sigIndex = FAN_SIG_TEST_003,
        .dst = &value, .size = sizeof(value), .timeout = A_TIMEOUT_NO_WAIT
    };
    assert(dataBusGet(&get) == A_STATUS_OK);
    return value;
}

static void write_speed(uint8_t unit, uint32_t value)
{
    uint8_t request[] = {unit, 16, 0, 34, 0, 2, 4,
        (uint8_t)(value >> 24U), (uint8_t)(value >> 16U),
        (uint8_t)(value >> 8U), (uint8_t)value};
    feed(request, sizeof(request));
}

static void recover(void)
{
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 0U);
}

#ifdef TEST_EXTENDED_LISTS
static void extra_map_tests(void)
{
    /* 离散映射正常读取，所有未映射的地址空洞都不能访问。 */
    feed((uint8_t[]){1, 3, 0, 16, 0, 2}, 6U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 9U);
    assert(memcmp(output + 3U,
                  (uint8_t[]){0x12, 0x34, 0x56, 0x78}, 4U) == 0);
    feed((uint8_t[]){1, 3, 0, 20, 0, 2}, 6U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 9U && output[6] == 100U);
    feed((uint8_t[]){1, 3, 0, 18, 0, 1}, 6U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 5U && output[2] == 2U);
    feed((uint8_t[]){1, 3, 0, 8, 0, 1}, 6U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 5U && output[2] == 2U);
    /* 完整写入也必须遵守只读映射权限。 */
    feed((uint8_t[]){1, 16, 0, 16, 0, 2, 4, 0, 0, 0, 1}, 11U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 5U && output[2] == 2U);
    assert(counter == 0x12345678U);
    /* 输入寄存器与保持寄存器可使用相同地址。 */
    feed((uint8_t[]){1, 4, 0, 0, 0, 2}, 6U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 9U && output[1] == 4U);
    assert(memcmp(output + 3U,
                  (uint8_t[]){0x12, 0x34, 0x56, 0x78}, 4U) == 0);
}
#endif

/* 43 个连续 U32 分两帧写入，再通过协议和总线验证所有目标。 */
static void bulk_test(void)
{
    for (size_t first = 0U; first < 43U; first += 60U) {
        size_t count = 43U - first;
        uint16_t address = (uint16_t)(288U + first * 2U);
        uint8_t command[247];
        if (count > 60U) count = 60U;
        command[0] = 1U; command[1] = 16U;
        command[2] = (uint8_t)(address >> 8U);
        command[3] = (uint8_t)address;
        command[4] = 0U; command[5] = (uint8_t)(count * 2U);
        command[6] = (uint8_t)(count * 4U);
        for (size_t i = 0U; i < count; i++) {
            uint32_t value = (uint32_t)(8000U + first + i);
            command[7U + i * 4U] = 0U;
            command[8U + i * 4U] = 0U;
            command[9U + i * 4U] = (uint8_t)(value >> 8U);
            command[10U + i * 4U] = (uint8_t)value;
        }
        feed(command, 7U + count * 4U);
        assert(testModbusProcess() == A_STATUS_OK && output_size == 8U);
        command[1] = 3U;
        feed(command, 6U);
        assert(testModbusProcess() == A_STATUS_OK);
        assert(output_size == 5U + count * 4U);
        for (size_t i = 0U; i < count; i++) {
            uint32_t value;
            const aBusGetIndexRequest_t get = {
                .deviceID = IDU_SIG_DEVICE_ID,
                .sigIndex = IDU_SIG_TEST_008 + first + i,
                .dst = &value, .size = sizeof(value),
                .timeout = A_TIMEOUT_NO_WAIT
            };
            assert(dataBusGet(&get) == A_STATUS_OK);
            assert(value == 8000U + first + i);
            assert(output[5U + i * 4U] == (uint8_t)(value >> 8U));
            assert(output[6U + i * 4U] == (uint8_t)value);
        }
    }
}

static void mixed_type_test(void)
{
    uint16_t values[18] = {
        200U, 50000U, 0U, 9999U, 0xFFFFU, 0xFF9CU,
        0x0102U, 0x0304U, 0x0506U, 0x0708U,
        0U, 5000U, 0xFFFFU, 0xFE0CU, 3U, 0x1122U, 0x3344U, 0U
    };
    uint8_t command[43] = {1U, 16U, 1U, 0U, 0U, 18U, 36U};
    IDUTest_t mixed;
    const aBusGetIndexRequest_t get = {
        .deviceID = IDU_SIG_DEVICE_ID, .sigIndex = IDU_SIG_TEST_007,
        .dst = &mixed, .size = sizeof(mixed), .timeout = A_TIMEOUT_NO_WAIT
    };
    for (size_t i = 0U; i < 18U; i++) {
        command[7U + i * 2U] = (uint8_t)(values[i] >> 8U);
        command[8U + i * 2U] = (uint8_t)values[i];
    }
    feed(command, sizeof(command));
    assert(testModbusProcess() == A_STATUS_OK && output_size == 8U);
    feed((uint8_t[]){1U, 3U, 1U, 0U, 0U, 18U}, 6U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 41U);
    assert(memcmp(output + 3U, command + 7U, 36U) == 0);
    assert(dataBusGet(&get) == A_STATUS_OK);
    assert(mixed.u32_value == 5000U && mixed.s32_value == -500);
    assert(mixed.u16_value == 3U && mixed.raw_value[2] == 0x33U);
    assert(mixed.raw_value[3] == 0x44U && mixed.reserved == 0U);
    /* 任一值越界或保留字段非零，整帧预检就拒绝提交。 */
    command[8] = 201U;
    command[30] = 1U;
    feed(command, sizeof(command));
    assert(testModbusProcess() == A_STATUS_OK && output[2] == 3U);
    assert(dataBusGet(&get) == A_STATUS_OK && mixed.u32_value == 5000U);
    command[8] = 200U;
    command[42] = 1U;
    feed(command, sizeof(command));
    assert(testModbusProcess() == A_STATUS_OK && output[2] == 3U);
    assert(dataBusGet(&get) == A_STATUS_OK && mixed.u32_value == 5000U);
}

static void callback_range_tests(void)
{
    uint32_t before, sampled, after;

    /* 地址段回调使用寄存器数值；线上的字节序和 CRC 由协议库负责。 */
    feed((uint8_t[]){1, 3, 2, 0, 0, 4}, 6U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 13U);
    assert(memcmp(output + 3U,
        (uint8_t[]){0, 10, 0, 20, 0, 30, 0, 40}, 8U) == 0);
    feed((uint8_t[]){1, 6, 2, 1, 0, 123}, 6U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 8U);
    feed((uint8_t[]){1, 16, 2, 2, 0, 2, 4, 0, 33, 0, 44}, 11U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 8U);
    /* 同一回调内先校验后写入，第二个值越界不会修改第一个值。 */
    feed((uint8_t[]){1, 16, 2, 0, 0, 2, 4, 0, 99, 0x27, 0x11}, 11U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 5U);
    assert(output[1] == 0x90 && output[2] == 3U);
    feed((uint8_t[]){1, 3, 2, 0, 0, 4}, 6U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 13U);
    assert(memcmp(output + 3U,
        (uint8_t[]){0, 10, 0, 123, 0, 33, 0, 44}, 8U) == 0);

    feed((uint8_t[]){1, 3, 2, 8, 0, 2}, 6U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 9U);
    assert(memcmp(output + 3U, (uint8_t[]){0x49, 0x44, 0x55, 0}, 4U) == 0);
    feed((uint8_t[]){1, 6, 2, 8, 0, 1}, 6U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 5U);
    assert(output[1] == 0x86 && output[2] == 2U);
    /* 两段之间的空洞不能自动补零或跳过。 */
    feed((uint8_t[]){1, 3, 2, 2, 0, 8}, 6U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 5U);
    assert(output[1] == 0x83 && output[2] == 2U);

    before = aOSGetUptimeMs();
    feed((uint8_t[]){1, 4, 2, 0, 0, 2}, 6U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 9U);
    after = aOSGetUptimeMs();
    sampled = ((uint32_t)output[3] << 24U) |
        ((uint32_t)output[4] << 16U) | ((uint32_t)output[5] << 8U) | output[6];
    assert((uint32_t)(sampled - before) <= (uint32_t)(after - before));
    assert(crc(output, output_size) == 0U);
}

static void server_tests(void)
{
    const uint8_t read[] = {1, 3, 0, 0, 0, 6};
    counter = 0x12345678U;
    feed(read, sizeof(read));
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 17U && crc(output, output_size) == 0U);
    assert(memcmp(output, (uint8_t[]){1, 3, 12, 0x12, 0x34,
           0x56, 0x78, 0, 0, 0, 100, 0, 0, 0, 25}, 15U) == 0);
    assert(waits == 2U); /* 发送前检查和发送后 TC 确认。 */
    callback_range_tests();
    bulk_test();
    mixed_type_test();
#ifdef TEST_EXTENDED_LISTS
    extra_map_tests();
#endif
    write_speed(1U, 4321U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 8U);
    assert(target_get() == 4321U);
    assert(motor_get().s32_value == 25);
    write_speed(1U, 6001U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 5U && output[1] == 0x90 && output[2] == 3U);
    assert(target_get() == 4321U);
    feed((uint8_t[]){1, 6, 0, 2, 0, 1}, 6U);
    assert(testModbusProcess() == A_STATUS_OK && output[2] == 2U);
    assert(target_get() == 4321U);
    feed((uint8_t[]){1, 16, 0, 4, 0, 2, 4, 255, 255, 255, 236}, 11U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 5U && output[2] == 2U);
    assert(motor_get().s32_value == 25);
    write_speed(0U, 456U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 0U);
    assert(target_get() == 456U);
    write_speed(2U, 789U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 0U);
    assert(target_get() == 456U);
    write_speed(1U, 789U);
    input[input_size - 1U] ^= 1U;
    assert(testModbusProcess() != A_STATUS_OK && output_size == 0U);
    assert(target_get() == 456U);
    recover();
    write_speed(1U, 789U);
    input[input_size++] = 0U;
    assert(testModbusProcess() != A_STATUS_OK && output_size == 0U);
    assert(target_get() == 456U);
    recover();
    write_speed(1U, 789U);
    input_size = 3U;
    assert(testModbusProcess() != A_STATUS_OK && output_size == 0U);
    recover();
    write_speed(1U, 789U);
    overflow = A_TRUE;
    assert(testModbusProcess() == A_STATUS_OK && output_size == 0U);
    recover();
    write_speed(1U, 789U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(target_get() == 789U);
    /* 消费任务暂停，两帧之间只有 2 ms，不能合并成一个 ADU。 */
    testAdvanceTime(3U);
    write_speed(0U, 111U);
    deliver();
    testAdvanceTime(2U);
    write_speed(0U, 222U);
    deliver();
    testAdvanceTime(2U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(target_get() == 111U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(target_get() == 222U);
    /* 完成时间差 800 us，扣除字符时间后仍小于 t1.5。 */
    testAdvanceTime(3U);
    write_speed(0U, 444U);
    for (size_t i = 0U; i < input_size; i++) {
        if (i == 4U) cycle_bias += 800U * 180U;
        rx_callback(rx_context, input[i], A_STATUS_OK);
    }
    input_pos = input_size;
    assert(testModbusProcess() == A_STATUS_OK);
    assert(target_get() == 444U);
    /* DWT 在两帧间回绕，仍须保持帧边界和顺序。 */
    testAdvanceTime(30U);
    cycle_bias = UINT32_MAX - 100000U - aOSGetUptimeMs() * 180000U;
    write_speed(0U, 555U);
    deliver();
    testAdvanceTime(2U);
    write_speed(0U, 666U);
    deliver();
    testAdvanceTime(2U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(target_get() == 555U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(target_get() == 666U);
    /* 三槽满后丢弃新帧，不能覆盖未消费的写入。 */
    for (uint32_t value = 1U; value <= 4U; value++) {
        testAdvanceTime(3U);
        write_speed(0U, value);
        deliver();
    }
    testAdvanceTime(3U);
    for (uint32_t value = 1U; value <= 3U; value++) {
        assert(testModbusProcess() == A_STATUS_OK);
        assert(target_get() == value);
    }
    assert(testModbusProcess() == A_STATUS_OK);
    assert(target_get() == 3U);
    /* 帧内 1 ms 间隔超过 t1.5，不能执行写入。 */
    testAdvanceTime(3U);
    write_speed(0U, 333U);
    for (size_t i = 0U; i < input_size; i++) {
        if (i == 4U) testAdvanceTime(1U);
        rx_callback(rx_context, input[i], A_STATUS_OK);
    }
    input_pos = input_size;
    assert(testModbusProcess() == A_STATUS_OK);
    assert(target_get() == 3U);
}
#else
static void fan_get(size_t sig_index, void *data, size_t size)
{
    const aBusGetIndexRequest_t request = {
        .deviceID = FAN_SIG_DEVICE_ID, .sigIndex = sig_index,
        .dst = data, .size = size, .timeout = A_TIMEOUT_NO_WAIT
    };
    assert(dataBusGet(&request) == A_STATUS_OK);
}

static void master_round(aStatus_t first_status, aStatus_t other_status)
{
    static const struct {
        uint16_t address;
        uint16_t quantity;
        size_t items;
    } groups[] = {
        {2U, 4U, 1U},
        {32U, 1U, 1U}, {34U, 3U, 2U}, {38U, 10U, 3U},
#ifdef TEST_EXTENDED_LISTS
        /* 移除断帧标记后，库自动恢复一帧读取连续的 23 个 U32。 */
        {64U, 46U, 23U}
#else
        {64U, 24U, 12U}, {88U, 22U, 11U}
#endif
    };
    const size_t count = sizeof(groups) / sizeof(groups[0]);
    size_t attempted = 0U;

    assert(rs485_link.poll->jobs[0].step_index == 0U);
    for (size_t i = 0U; i < count; i++) {
        output_size = 0U;
        assert(testModbusProcess() == (i == 0U ? first_status : other_status));
        assert(last_address == groups[i].address);
        assert(last_quantity == groups[i].quantity);
        assert(output_size == 8U); /* 一组恰好一帧，不是逐点读取。 */
        attempted += groups[i].items;
        assert(rs485_link.poll->jobs[0].cycle_active == (i + 1U != count));
        assert(rs485_link.poll->jobs[0].step_index ==
               (i + 1U == count ? 0U : i + 1U));
    }
    assert(attempted == FAN_modbus_master_config.service.poll_count);
}

static void fan_values_check(void)
{
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    int32_t s32;
    FANTestRaw_t raw;
    FANTest_t combined;
    const uint8_t expected_raw[] = {
        0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U
    };

    fan_get(FAN_SIG_TEST_002, &u8, sizeof(u8));
    assert(u8 == 1U);
    fan_get(FAN_SIG_TEST_003, &u32, sizeof(u32));
    assert(u32 == 2345U);
    fan_get(FAN_SIG_TEST_004, &u16, sizeof(u16));
    assert(u16 == 2U);
    fan_get(FAN_SIG_TEST_005, &s32, sizeof(s32));
    assert(s32 == -42);
    fan_get(FAN_SIG_TEST_006, raw, sizeof(raw));
    assert(memcmp(raw, expected_raw, sizeof(raw)) == 0);
    fan_get(FAN_SIG_TEST_007, &combined, sizeof(combined));
    assert(combined.u32_value == 567U && combined.s32_value == -89);
    for (size_t i = FAN_SIG_TEST_008; i < FAN_SIG_COUNT; i++) {
        fan_get(i, &u32, sizeof(u32));
        if (i <= FAN_SIG_TEST_030) assert(u32 == 1001U + i);
        else assert(u32 == i + 1U); /* 未选的 20 项保持初始化内容。 */
    }
}

static unsigned temporary_completed;
static uint16_t temporary_values[2];
static protocolModbusRequest_t temporary_request;

static void temporary_complete(void *context, aStatus_t status,
                               const aModbusResult_t *response)
{
    assert(context == &temporary_values);
    assert(status == A_STATUS_OK && response->exception == 0U);
    temporary_completed++;
}

static void temporary_tests(void)
{
    aPollProcessResult_t result;
    aPollJobState_t *state = &rs485_link.poll->jobs[0];
    /* 让周期作业等待一秒，验证通知可以提前唤醒且不直接发送报文。 */
    state->cycle_active = A_FALSE;
    state->step_index = 0U;
    state->cycle_started = aOSGetUptimeMs();
    state->delay = aTimepointCalc(A_TIMEOUT_NO_WAIT, aOSGetUptimeMs());
    aAtomicStore(&state->triggered, A_FALSE);
    expected_poll = 0U;
    protocolModbusRequestStructInit(&temporary_request);
    temporary_request.access.read.access.address = 34U;
    temporary_request.access.read.access.quantity = 2U;
    temporary_request.access.read.access.data = temporary_values;
    temporary_request.access.read.access.size = sizeof(temporary_values);
    temporary_request.context = &temporary_values;
    temporary_request.complete = temporary_complete;
    output_size = 0U;
    assert(protocolModbusSubmit(&temporary_request) == A_STATUS_OK);
    assert(output_size == 0U && temporary_completed == 0U);
    assert(aPollProcess(rs485_link.poll, &result) == A_STATUS_OK);
    assert(result.type == APOLL_WORK_REQUEST);
    assert(temporary_completed == 1U && temporary_values[1] == 2345U);
    temporary_request.write = A_TRUE;
    aModbusClientWriteRequestStructInit(&temporary_request.access.write);
    temporary_values[0] = 1U;
    temporary_request.access.write.access.address = 32U;
    temporary_request.access.write.access.quantity = 1U;
    temporary_request.access.write.access.data = temporary_values;
    temporary_request.access.write.access.size = sizeof(temporary_values);
    output_size = 0U;
    assert(protocolModbusSubmit(&temporary_request) == A_STATUS_OK);
    assert(aPollProcess(rs485_link.poll, &result) == A_STATUS_OK);
    assert(result.type == APOLL_WORK_REQUEST && temporary_completed == 2U);
    assert(output[1] == 6U && output_size == 8U);
    assert(protocolRefresh() == A_STATUS_OK);
    assert(protocolRefresh() == A_STATUS_OK);
    assert(aPollWait(rs485_link.poll, A_TIMEOUT_FOREVER) == A_STATUS_OK);
    assert(aPollProcess(rs485_link.poll, &result) == A_STATUS_OK);
    assert(result.type == APOLL_WORK_EVENT);
    assert(aPollProcess(rs485_link.poll, &result) == A_STATUS_OK);
    assert(result.type == APOLL_WORK_JOB && result.step_index == 0U);
}

static void master_tests(void)
{
    aBool_t seen[FAN_SIG_COUNT] = {A_FALSE};
    size_t selected = 0U;
    unsigned allocations = testAllocations();

    assert(FAN_SIG_COUNT == 50U);
    assert(FAN_modbus_master_config.service.poll_count == 30U);
    for (size_t i = 0U; i < FAN_modbus_master_config.service.poll_count; i++) {
        const aModbusSigTarget_t *target =
            &FAN_modbus_master_config.service.polls[i].target;
        assert(target->deviceID == FAN_SIG_DEVICE_ID);
        assert(target->sigIndex <= FAN_SIG_TEST_030);
        if (!seen[target->sigIndex]) selected++;
        seen[target->sigIndex] = A_TRUE;
    }
    assert(selected == 30U);
    master_round(A_STATUS_OK, A_STATUS_OK);
    assert(motor_get().u32_value == 4321U);
    assert(motor_get().s32_value == -123);
    fan_values_check();
    response_value = 6001U;
    master_round(A_STATUS_INVALID_PARAM, A_STATUS_OK);
    assert(motor_get().u32_value == 4321U);
    response_enabled = A_FALSE;
    master_round(A_STATUS_TIMEOUT, A_STATUS_TIMEOUT);
    assert(motor_get().u32_value == 4321U);
    fan_values_check();
    response_enabled = A_TRUE;
    response_value = 321U;
    response_extra = A_TRUE;
    master_round(A_STATUS_ERROR, A_STATUS_OK);
    assert(motor_get().u32_value == 4321U);
    response_extra = A_FALSE;
    master_round(A_STATUS_OK, A_STATUS_OK);
    assert(motor_get().u32_value == 321U);
    assert(motor_get().s32_value == -123);
    fan_values_check();
    assert(testAllocations() == allocations);
    check_reentry = A_FALSE;
    temporary_tests();
}
#endif

static void portReceive(void *context, uint8_t byte, aStatus_t status)
{
    (void)context;
    (void)byte;
    (void)status;
}

/* 物理端口不创建协议实例，也可以交给其他逐字节接收协议使用。 */
static void port_tests(void)
{
    rs485Port_t port;
    unsigned baseline = testAllocations();
    const uint8_t byte = 0U;

    assert(rs485PortPrepare(NULL) == A_STATUS_INVALID_PARAM);
    assert(rs485PortClose() == A_STATUS_NOT_READY);
    assert(rs485PortPrepare(&port) == A_STATUS_OK);
    assert(port.output.read == NULL && port.output.write != NULL);
    assert(port.output.flush == NULL);
    assert(port.baud_rate == 115200U && port.character_bits == 10U);
    assert(port.ticks_per_second == 180000000U);
    assert(port.output.write(&byte, 1U, A_TIMEOUT_NO_WAIT) == -1);
    assert(rs485PortOpen(NULL, NULL) == A_STATUS_INVALID_PARAM);
    assert(rs485PortOpen(portReceive, NULL) == A_STATUS_OK);
    assert(rs485PortOpen(portReceive, NULL) == A_STATUS_BUSY);
    assert(rs485PortPrepare(&port) == A_STATUS_BUSY);
    close_busy = A_TRUE;
    assert(rs485PortClose() == A_STATUS_BUSY);
    rx_callback(rx_context, 0U, A_STATUS_ERROR);
    close_busy = A_FALSE;
    assert(rs485PortClose() == A_STATUS_OK);
    assert(opened == closed && testAllocations() == baseline);
}

#if APP_MODBUS_MASTER_ENABLE
static void reset_poll(void)
{
    aPollJobState_t *state = &rs485_link.poll->jobs[0];
    state->step_index = 0U;
    state->cycle_active = A_FALSE;
    state->delay = aTimepointCalc(A_TIMEOUT_NO_WAIT, aOSGetUptimeMs());
    aAtomicStore(&state->triggered, A_TRUE);
    aAtomicStore(&rs485_link.poll->active, A_FALSE);
    expected_poll = 0U;
}
#endif

static void task_tests(void)
{
    process_count = 0U;
    delay_count = 0U;
    task_test = A_TRUE;
#if APP_MODBUS_MASTER_ENABLE
    reset_poll();
#endif
    if (setjmp(task_done) == 0) {
        created_task.function(created_task.argument);
    }
    assert(process_count == 7U);
#if APP_MODBUS_MASTER_ENABLE
    uint32_t elapsed = 0U;
    const aPollJobConfig_t *schedule = &rs485_link.config->product->schedule;
    assert(delay_count == 7U);
    for (size_t i = 0U; i < delay_count; i++) {
        uint32_t expected = task_results[i] == A_STATUS_OK ?
                            schedule->interval_ms : schedule->error_delay_ms;
        if ((i + 1U) % aModbusPlanFrameCount(rs485_link.plan) == 0U) {
            uint32_t remaining = schedule->poll_time_ms > elapsed ?
                                 schedule->poll_time_ms - elapsed : 0U;
            if (remaining > expected) expected = remaining;
            elapsed = 0U;
        } else elapsed += expected;
        assert(task_delays[i] == expected);
    }
    /* longjmp 仅供测试退出永循环；清除被中断的 Process 所有权。 */
    reset_poll();
#else
    assert(delay_count == 2U);
    assert(task_delays[0] == 5U && task_delays[1] == 5U);
#endif
    task_test = A_FALSE;
}

int main(int argc, char **argv)
{
    unsigned baseline;

    if (argc == 2) {
        aStatus_t expected = A_STATUS_OK;
        if (strcmp(argv[1], "clock") == 0) {
            fail_clock = A_TRUE;
            expected = A_STATUS_UNSUPPORTED;
        } else if (strcmp(argv[1], "task") == 0) {
            fail_task = A_TRUE;
            expected = A_STATUS_NO_MEMORY;
        } else if (strcmp(argv[1], "poll") == 0) {
            expected = A_STATUS_NOT_FOUND;
        } else if (strcmp(argv[1], "mapping") == 0 ||
                   strcmp(argv[1], "length") == 0 ||
                   strcmp(argv[1], "layout") == 0) {
            expected = A_STATUS_INVALID_PARAM;
        } else {
            assert(strcmp(argv[1], "startup") == 0);
            run_immediately = A_TRUE;
        }
        assert(busInit() == A_STATUS_OK);
        assert(protocolInit(bus_handle) == expected);
        assert(protocolInit(bus_handle) == A_STATUS_BUSY);
        assert(motor_get().u32_value == 100U);
        if (expected == A_STATUS_OK) assert(modbusDeInit(&rs485_link) == A_STATUS_OK);
        assert(opened == closed);
        if (strcmp(argv[1], "poll") == 0 ||
            strcmp(argv[1], "length") == 0 ||
            strcmp(argv[1], "layout") == 0) assert(opened == 0U);
        return 0;
    }
    assert(testModbusProcess() == A_STATUS_NOT_READY);
    assert(testModbusInit(NULL) == A_STATUS_INVALID_PARAM);
    assert(opened == closed);
    assert(busInit() == A_STATUS_OK);
    baseline = testAllocations();
    fail_clock = A_TRUE;
    assert(testModbusInit(bus_handle) == A_STATUS_UNSUPPORTED);
    fail_clock = A_FALSE;
    fail_serial = A_TRUE;
    assert(testModbusInit(bus_handle) == A_STATUS_ERROR);
    fail_serial = A_FALSE;
#if AMODBUS_DYNAMIC_ENABLE
    testFailAllocation(A_TRUE);
    assert(testModbusInit(bus_handle) == A_STATUS_NO_MEMORY);
    testFailAllocation(A_FALSE);
#endif
    assert(opened == closed && testAllocations() == baseline);
    assert(testModbusInit(bus_handle) == A_STATUS_OK);
    assert(testModbusInit(bus_handle) == A_STATUS_BUSY);
    assert(testModbusDeInit() == A_STATUS_OK);
    fail_task = A_TRUE;
    assert(modbusInit(&rs485_link, bus_handle) == A_STATUS_NO_MEMORY);
    assert(testModbusProcess() == A_STATUS_NOT_READY);
    assert(opened == closed && testAllocations() == baseline);
    fail_task = A_FALSE;
    run_immediately = A_TRUE;
    assert(modbusInit(&rs485_link, bus_handle) == A_STATUS_OK);
    run_immediately = A_FALSE;
#if APP_MODBUS_MASTER_ENABLE
    check_reentry = A_TRUE;
    master_tests();
    check_reentry = A_FALSE;
#else
    server_tests();
#endif
    task_tests();
    close_busy = A_TRUE;
    assert(testModbusDeInit() == A_STATUS_BUSY);
    assert(testModbusProcess() == A_STATUS_NOT_READY);
    assert(testModbusInit(bus_handle) == A_STATUS_BUSY);
    /* 关闭未完成时 RX 仍可能到来，适配实例不能提前释放。 */
    rx_callback(rx_context, 0U, A_STATUS_ERROR);
    close_busy = A_FALSE;
    assert(testModbusDeInit() == A_STATUS_OK);
    assert(opened == closed && testAllocations() == baseline);
    port_tests();
    puts("Modbus demo application/transport/data/lifecycle passed");
    return 0;
}
