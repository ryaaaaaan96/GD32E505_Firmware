/* 真实应用端口 + aModbus/nanoMODBUS + aBus，模拟串口线路。 */
#include "app_config.h"
#include "../../app/protocol/protocol.c"
#define testModbusInit modbusInit
#define testModbusProcess modbusProcess
#define testModbusDeInit modbusDeInit
#include "IDU_sig_table.h"
#include "data_bus_service.h"
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
                     IDU_SIG_COUNTER,
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
static uint32_t task_delays[5];
static const aStatus_t task_results[] = {
    A_STATUS_OK, A_STATUS_OK, A_STATUS_TIMEOUT, A_STATUS_TIMEOUT, A_STATUS_OK
};

aStatus_t __real_aModbusClientReadSig(aModbusHandle_t *handle,
    const aModbusClientSigRequest_t *request);
aStatus_t __real_aModbusServerProcess(aModbusHandle_t *handle,
    const aModbusServerProcessRequest_t *request);

static aStatus_t nextResult(void)
{
    if (process_count == 5U) longjmp(task_done, 1);
    return task_results[process_count++];
}

aStatus_t __wrap_aModbusClientReadSig(aModbusHandle_t *handle,
    const aModbusClientSigRequest_t *request)
{
    aStatus_t status;

#if APP_MODBUS_MASTER_ENABLE
    assert(request == &FAN_modbus_master_config.polls[expected_poll]);
#endif
    status = task_test ? nextResult() :
                        __real_aModbusClientReadSig(handle, request);
#if APP_MODBUS_MASTER_ENABLE
    if (status != A_STATUS_BUSY) {
        expected_poll++;
        if (expected_poll == FAN_modbus_master_config.poll_count) expected_poll = 0U;
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
static uint8_t last_function;
static aBool_t check_reentry;
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
    last_function = frame[1];
    assert(last_function == 3U || last_function == 16U);
    assert(size == (last_function == 3U ? 8U : 13U));
    assert(frame[0] == 1U);
    assert(memcmp(frame + 2, (uint8_t[]){0, 2, 0, 2}, 4U) == 0);
    assert(crc(data, size) == 0U);
    if (check_reentry) {
        assert(testModbusProcess() == A_STATUS_BUSY);
    }
    if (response_enabled) {
        uint8_t response[] = {1, 3, 4,
            (uint8_t)(response_value >> 24U),
            (uint8_t)(response_value >> 16U),
            (uint8_t)(response_value >> 8U), (uint8_t)response_value};
        if (last_function == 3U) feed(response, sizeof(response));
        else feed(frame, 6U); /* FC10 响应只回显地址和寄存器数量。 */
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
aStatus_t appSigTaskInit(void) { return A_STATUS_OK; }

void aOSDelayMs(uint32_t ms)
{
    if (task_test) {
        assert(delay_count < 5U);
        task_delays[delay_count++] = ms;
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

static FANMotor_t motor_get(void)
{
    FANMotor_t motor;
    aBusGetIndexRequest_t get;
    aBusGetIndexRequestStructInit(&get);
    get.deviceID = FAN_SIG_DEVICE_ID;
    get.sigIndex = FAN_SIG_MOTOR;
    get.dst = &motor;
    get.size = sizeof(motor);
    assert(dataBusGet(&get) == A_STATUS_OK);
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
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 0U);
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
    write_speed(1U, 4321U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 8U);
    assert(motor_get().speed == 4321U);
    assert(motor_get().temperature == 25);
    write_speed(1U, 6001U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(output_size == 5U && output[1] == 0x90 && output[2] == 3U);
    assert(motor_get().speed == 4321U);
    feed((uint8_t[]){1, 6, 0, 2, 0, 1}, 6U);
    assert(testModbusProcess() == A_STATUS_OK && output[2] == 2U);
    assert(motor_get().speed == 4321U);
    feed((uint8_t[]){1, 16, 0, 4, 0, 2, 4, 255, 255, 255, 236}, 11U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(motor_get().temperature == -20);
    write_speed(0U, 456U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 0U);
    assert(motor_get().speed == 456U);
    write_speed(2U, 789U);
    assert(testModbusProcess() == A_STATUS_OK && output_size == 0U);
    assert(motor_get().speed == 456U);
    write_speed(1U, 789U);
    input[input_size - 1U] ^= 1U;
    assert(testModbusProcess() != A_STATUS_OK && output_size == 0U);
    assert(motor_get().speed == 456U);
    recover();
    write_speed(1U, 789U);
    input[input_size++] = 0U;
    assert(testModbusProcess() != A_STATUS_OK && output_size == 0U);
    assert(motor_get().speed == 456U);
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
    assert(motor_get().speed == 789U);
    /* 消费任务暂停，两帧之间只有 2 ms，不能合并成一个 ADU。 */
    testAdvanceTime(3U);
    write_speed(0U, 111U);
    deliver();
    testAdvanceTime(2U);
    write_speed(0U, 222U);
    deliver();
    testAdvanceTime(2U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(motor_get().speed == 111U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(motor_get().speed == 222U);
    /* 完成时间差 800 us，扣除字符时间后仍小于 t1.5。 */
    testAdvanceTime(3U);
    write_speed(0U, 444U);
    for (size_t i = 0U; i < input_size; i++) {
        if (i == 4U) cycle_bias += 800U * 180U;
        rx_callback(rx_context, input[i], A_STATUS_OK);
    }
    input_pos = input_size;
    assert(testModbusProcess() == A_STATUS_OK);
    assert(motor_get().speed == 444U);
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
    assert(motor_get().speed == 555U);
    assert(testModbusProcess() == A_STATUS_OK);
    assert(motor_get().speed == 666U);
    /* 三槽满后丢弃新帧，不能覆盖未消费的写入。 */
    for (uint32_t value = 1U; value <= 4U; value++) {
        testAdvanceTime(3U);
        write_speed(0U, value);
        deliver();
    }
    testAdvanceTime(3U);
    for (uint32_t value = 1U; value <= 3U; value++) {
        assert(testModbusProcess() == A_STATUS_OK);
        assert(motor_get().speed == value);
    }
    assert(testModbusProcess() == A_STATUS_OK);
    assert(motor_get().speed == 3U);
    /* 帧内 1 ms 间隔超过 t1.5，不能执行写入。 */
    testAdvanceTime(3U);
    write_speed(0U, 333U);
    for (size_t i = 0U; i < input_size; i++) {
        if (i == 4U) testAdvanceTime(1U);
        rx_callback(rx_context, input[i], A_STATUS_OK);
    }
    input_pos = input_size;
    assert(testModbusProcess() == A_STATUS_OK);
    assert(motor_get().speed == 3U);
}
#else
static void master_tests(void)
{
    assert(testModbusProcess() == A_STATUS_OK);
    assert(motor_get().speed == 4321U);
    assert(motor_get().temperature == 25);
    response_value = 6001U;
    assert(testModbusProcess() != A_STATUS_OK);
    assert(motor_get().speed == 4321U);
    response_enabled = A_FALSE;
    assert(testModbusProcess() == A_STATUS_TIMEOUT);
    assert(motor_get().speed == 4321U);
    response_enabled = A_TRUE;
    response_value = 321U;
    response_extra = A_TRUE;
    assert(testModbusProcess() != A_STATUS_OK);
    assert(motor_get().speed == 4321U);
    response_extra = A_FALSE;
    assert(testModbusProcess() == A_STATUS_OK);
    assert(motor_get().speed == 321U);
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

static void task_tests(void)
{
    process_count = 0U;
    delay_count = 0U;
    task_test = A_TRUE;
    if (setjmp(task_done) == 0) {
        created_task.function(created_task.argument);
    }
    assert(process_count == 5U);
#if APP_MODBUS_MASTER_ENABLE
    assert(delay_count == 5U);
    for (size_t i = 0U; i < delay_count; i++) {
        assert(task_delays[i] == 1000U);
    }
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
        } else {
            assert(strcmp(argv[1], "startup") == 0);
            run_immediately = A_TRUE;
        }
        assert(protocolInit() == expected);
        assert(protocolInit() == A_STATUS_BUSY);
        assert(motor_get().speed == 100U);
        if (expected == A_STATUS_OK) assert(modbusDeInit() == A_STATUS_OK);
        assert(opened == closed);
        return 0;
    }
    assert(testModbusProcess() == A_STATUS_NOT_READY);
    assert(testModbusInit() == A_STATUS_NOT_READY);
    assert(opened == closed);
    assert(tablesInit() == A_STATUS_OK);
    baseline = testAllocations();
    fail_clock = A_TRUE;
    assert(testModbusInit() == A_STATUS_UNSUPPORTED);
    fail_clock = A_FALSE;
    fail_serial = A_TRUE;
    assert(testModbusInit() == A_STATUS_ERROR);
    fail_serial = A_FALSE;
#if AMODBUS_DYNAMIC_ENABLE
    testFailAllocation(A_TRUE);
    assert(testModbusInit() == A_STATUS_NO_MEMORY);
    testFailAllocation(A_FALSE);
#endif
    assert(opened == closed && testAllocations() == baseline);
    assert(testModbusInit() == A_STATUS_OK);
    assert(testModbusInit() == A_STATUS_BUSY);
    assert(testModbusDeInit() == A_STATUS_OK);
    fail_task = A_TRUE;
    assert(modbusInit() == A_STATUS_NO_MEMORY);
    assert(testModbusProcess() == A_STATUS_NOT_READY);
    assert(opened == closed && testAllocations() == baseline);
    fail_task = A_FALSE;
    run_immediately = A_TRUE;
    assert(modbusInit() == A_STATUS_OK);
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
    assert(testModbusInit() == A_STATUS_BUSY);
    /* 关闭未完成时 RX 仍可能到来，适配实例不能提前释放。 */
    rx_callback(rx_context, 0U, A_STATUS_ERROR);
    close_busy = A_FALSE;
    assert(testModbusDeInit() == A_STATUS_OK);
    assert(opened == closed && testAllocations() == baseline);
    port_tests();
    puts("Modbus demo application/transport/data/lifecycle passed");
    return 0;
}
