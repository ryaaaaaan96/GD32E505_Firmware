/* 不链接 aDrv/aDev，验证通用分帧、多实例、低波特率及生命周期。 */
#include "aComm_instance.h"
#include "aModbus.h"
#include "aOS.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

void testSetTime(uint32_t ms);
unsigned testAllocations(void);
void testFailAllocation(aBool_t fail);

/* 常量表达式可直接初始化只读配置；大数计算不能先在 32 位内溢出。 */
_Static_assert(AMODBUS_RTU_WIRE_TIME_MS(115200U, 10U, 0U) == 0U,
               "empty transfer");
_Static_assert(AMODBUS_RTU_WIRE_TIME_MS(115200U, 10U, 1U) == 1U,
               "round up sub-millisecond transfer");
_Static_assert(AMODBUS_RTU_WIRE_TIME_MS(115200U, 10U, 256U) == 23U,
               "maximum frame");
_Static_assert(AMODBUS_RTU_WIRE_TIME_MS(9600U, 11U, 256U) == 294U,
               "parity and low baud rate");
_Static_assert(AMODBUS_RTU_WIRE_TIME_MS(1200U, 10U, 256U) == 2134U,
               "slow transfer");
_Static_assert(AMODBUS_RTU_WIRE_TIME_MS(1U, 16U, UINT32_MAX) ==
               UINT64_C(68719476720000), "64-bit intermediate");
static const aTimeout_t io_timeout =
    AMODBUS_RTU_TIMEOUT_INIT(115200U, 10U, 256U, 20U);
static const aTimeout_t transaction_timeout =
    AMODBUS_RTU_TIMEOUT_INIT(115200U, 10U, 512U, 500U);
static const aCommTiming_t static_timing = {
    .character_us = AMODBUS_RTU_CHARACTER_US(19200U, 11U),
    .byte_gap_us = AMODBUS_RTU_BYTE_GAP_US(19200U, 11U),
    .frame_gap_us = AMODBUS_RTU_FRAME_GAP_US(19200U, 11U),
};

static uint64_t now_us;
typedef struct {
    pthread_mutex_t mutex;
    aModbusRtuFilter_t filter;
    unsigned clears, writes, waits, flushes;
} link_t;

static void advance(uint32_t us)
{
    now_us += us;
    testSetTime((uint32_t)(now_us / 1000U));
}

void aOSDelayMs(uint32_t ms) { advance(ms * 1000U); }
static uint32_t ticks(void *context)
{
    (void)context;
    return (uint32_t)now_us;
}
static void enter(void *context)
{
    link_t *link = context;
    assert(pthread_mutex_lock(&link->mutex) == 0);
}
static void leave(void *context)
{
    link_t *link = context;
    assert(pthread_mutex_unlock(&link->mutex) == 0);
}
static aSSize_t write_bytes(void *context, const void *data, size_t size,
                           aTimeout_t timeout)
{
    link_t *link = context;
    (void)data;
    (void)timeout;
    link->writes++;
    return (aSSize_t)size;
}
static aStatus_t flush_bytes(void *context, aTimeout_t timeout)
{
    link_t *link = context;
    assert(aTimeoutIsValid(timeout));
    link->flushes++;
    return A_STATUS_OK;
}
static aStatus_t wait_transmit(void *context, aTimeout_t timeout)
{
    link_t *link = context;
    (void)timeout;
    link->waits++;
    return A_STATUS_OK;
}
static void clear_error(void *context)
{
    link_t *link = context;
    link->clears++;
}
static aStatus_t configure_timing(aCommChannelConfig_t *config,
    uint32_t baud, uint8_t bits)
{
    aModbusRtuTiming_t timing;
    aStatus_t status = aModbusRtuTimingDefaults(baud, bits, &timing);
    if (status == A_STATUS_OK) {
        config->timing = (aCommTiming_t){
            .character_us = timing.character_us,
            .byte_gap_us = timing.byte_gap_us,
            .frame_gap_us = timing.frame_gap_us
        };
    }
    return status;
}

static aCommChannelConfig_t configuration(link_t *link)
{
    aCommChannelConfig_t config;
    aCommChannelConfigStructInit(&config);
    config.mode = ACOMM_CHANNEL_GAP;
    link->filter = (aModbusRtuFilter_t){AMODBUS_ROLE_SERVER, 1U};
    config.frame_check = aModbusRtuFrameCheck;
    config.frame_context = &link->filter;
    config.io.context = link;
    config.io.ticks_per_second = 1000000U;
    config.io.ticks = ticks;
    config.io.enter = enter;
    config.io.exit = leave;
    config.stream.context = link;
    config.stream.write = write_bytes;
    config.io.wait_transmit_complete = wait_transmit;
    config.io.clear_error = clear_error;
    assert(configure_timing(&config, 115200U, 10U) == A_STATUS_OK);
    return config;
}
static void feed(aCommChannel_t *handle, const uint8_t *data, size_t size)
{
    const aCommChannelIo_t *io = &handle->config.io;
    io->enter(io->context);
    for (size_t i = 0U; i < size; i++) {
        aCommChannelReceive(handle, data[i], A_STATUS_OK);
    }
    io->exit(io->context);
}

static void multi_instance(void)
{
    link_t first = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    link_t second = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    aCommChannelConfig_t config = configuration(&first);
    aCommChannel_t storage, *other;
    aCommTransport_t one, two;
    uint8_t data[20];
    const uint8_t request[] = {1, 3, 0, 0, 0, 1, 0, 0};
    const uint8_t response[] = {2, 3, 2, 0, 9, 0, 0};
    unsigned baseline = testAllocations();

    config.stream.flush = flush_bytes;
    assert(aCommChannelInitStatic(&config, &storage) == A_STATUS_OK);
    config = configuration(&second);
    second.filter.role = AMODBUS_ROLE_CLIENT;
    assert(aCommChannelCreate(&config, &other) == A_STATUS_OK);
    assert(aCommChannelBindTransport(&storage, &one) == A_STATUS_OK);
    assert(aCommChannelBindTransport(other, &two) == A_STATUS_OK);
    assert(one.context != two.context && one.finish != NULL);
    assert(one.stream.context == &storage && two.stream.context == other);
    assert(one.stream.read == two.stream.read);
    assert(one.stream.write == two.stream.write);
    assert(one.stream.flush != NULL && two.stream.flush == NULL);
    assert(one.stream.flush(one.stream.context, A_TIMEOUT_MS(5U)) ==
           A_STATUS_OK);
    assert(first.flushes == 1U && second.flushes == 0U);
    assert(one.stream.read(one.stream.context, NULL, 0U,
                           A_TIMEOUT_NO_WAIT) == 0);
    assert(one.stream.write(one.stream.context, NULL, 0U,
                            A_TIMEOUT_NO_WAIT) == 0);
    assert(first.writes == 0U);
    assert(one.stream.read(one.stream.context, data, sizeof(data),
                           A_TIMEOUT_NO_WAIT) == -1);
    assert(aOSGetErrno() == A_EAGAIN);
    assert(one.stream.read(one.stream.context, data, sizeof(data),
                           A_TIMEOUT_MS(1U)) == -1);
    assert(aOSGetErrno() == A_ETIMEDOUT);
    advance(3000U);
    feed(&storage, request, sizeof(request));
    feed(other, response, sizeof(response));
    advance(3000U);
    feed(&storage, request, sizeof(request));
    advance(3000U);
    assert(one.stream.read(
            one.stream.context, data, 3U, A_TIMEOUT_NO_WAIT) == 3);
    assert(memcmp(data, request, 3U) == 0);
    assert(one.stream.read(
            one.stream.context, data, sizeof(data), A_TIMEOUT_NO_WAIT) == 5);
    assert(memcmp(data, request + 3U, 5U) == 0);
    assert(one.stream.read(
            one.stream.context, data, sizeof(data), A_TIMEOUT_NO_WAIT) == -1);
    assert(aOSGetErrno() == A_EIO);
    one.finish(one.context);
    assert(one.stream.read(
            one.stream.context, data, sizeof(data), A_TIMEOUT_NO_WAIT) == 8);
    assert(memcmp(data, request, sizeof(request)) == 0);
    assert(two.stream.read(
            two.stream.context, data, sizeof(data), A_TIMEOUT_NO_WAIT) == 7);
    assert(memcmp(data, response, sizeof(response)) == 0);
    one.finish(one.context);
    two.finish(two.context);
    assert(one.discard_input(one.context, A_TIMEOUT_MS(5U)) == A_STATUS_OK);
    assert(first.clears == 1U && second.clears == 0U);
    assert(one.prepare_frame(one.context, A_TIMEOUT_MS(5U)) == A_STATUS_OK);
    assert(one.stream.write(one.stream.context, request, sizeof(request),
                     A_TIMEOUT_MS(5U)) == 8);
    assert(one.wait_transmit_complete(one.context,
                                      A_TIMEOUT_MS(5U)) == A_STATUS_OK);
    assert(first.writes == 1U && second.writes == 0U);
    assert(two.stream.write(two.stream.context, request, sizeof(request),
                            A_TIMEOUT_MS(5U)) == 8);
    assert(first.writes == 1U && second.writes == 1U);
    assert(testAllocations() == baseline + 1U);
    assert(aCommChannelDestroy(&storage) == A_STATUS_INVALID_PARAM);
    assert(aCommChannelDeInitStatic(other) == A_STATUS_INVALID_PARAM);
    assert(aCommChannelDeInitStatic(&storage) == A_STATUS_OK);
    assert(aCommChannelBindTransport(&storage, &one) == A_STATUS_NOT_READY);
    assert(aCommChannelDestroy(other) == A_STATUS_OK);
    assert(testAllocations() == baseline);
    assert(pthread_mutex_destroy(&first.mutex) == 0);
    assert(pthread_mutex_destroy(&second.mutex) == 0);
}

static void timing(uint32_t baud, uint8_t bits, uint32_t gap,
                   uint32_t quiet)
{
    link_t link = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    aCommChannelConfig_t config = configuration(&link);
    aCommChannel_t handle;
    aCommTransport_t transport;
    uint8_t data[8];
    const uint8_t request[] = {7, 3, 0, 0, 0, 1, 0, 0};
    assert(configure_timing(&config, baud, bits) == A_STATUS_OK);
    link.filter.unit_id = 7U;
    assert(aCommChannelInitStatic(&config, &handle) == A_STATUS_OK);
    assert(aCommChannelBindTransport(&handle, &transport) == A_STATUS_OK);
    advance(quiet);
    feed(&handle, request, 4U);
    advance(gap);
    feed(&handle, request + 4U, 4U);
    advance(quiet);
    assert(transport.stream.read(transport.stream.context, data, sizeof(data),
                          A_TIMEOUT_NO_WAIT) == 8);
    assert(memcmp(data, request, sizeof(data)) == 0);
    transport.finish(transport.context);
    /* 超出帧内间隔，但没有到帧间静默，整帧必须丢弃。 */
    feed(&handle, request, 4U);
    advance(handle.byte_gap + handle.character_ticks + 100U);
    feed(&handle, request + 4U, 4U);
    advance(quiet);
    assert(transport.stream.read(transport.stream.context, data, sizeof(data),
                          A_TIMEOUT_MS(1U)) == -1);
    assert(handle.dropped_frames == 1U);
    assert(aCommChannelDeInitStatic(&handle) == A_STATUS_OK);
    assert(pthread_mutex_destroy(&link.mutex) == 0);
}

static void failures(void)
{
    link_t link = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    aCommChannelConfig_t config = configuration(&link);
    aCommChannel_t handle, *created = (void *)1;
    assert(aCommChannelCreate(NULL, &created) == A_STATUS_INVALID_PARAM);
    assert(created == NULL);
    config.timing.frame_gap_us = 0U;
    assert(aCommChannelInitStatic(&config, &handle) == A_STATUS_INVALID_PARAM);
    config = configuration(&link);
    config.io.ticks = NULL;
    assert(aCommChannelInitStatic(&config, &handle) == A_STATUS_INVALID_PARAM);
    config = configuration(&link);
    config.io.ticks_per_second = UINT32_MAX;
    assert(configure_timing(&config, 1U, 10U) == A_STATUS_OK);
    assert(aCommChannelInitStatic(&config, &handle) == A_STATUS_UNSUPPORTED);
    assert(configure_timing(&config, 0U, 10U) == A_STATUS_INVALID_PARAM);
    config = configuration(&link);
    testFailAllocation(A_TRUE);
    assert(aCommChannelCreate(&config, &created) == A_STATUS_NO_MEMORY);
    assert(created == NULL);
    testFailAllocation(A_FALSE);
    assert(pthread_mutex_destroy(&link.mutex) == 0);
}

/* 调用者持有生产者保护；时间代表采集记录，不推进交付时钟。 */
static void feed_at(aCommChannel_t *handle, const uint8_t *data,
                    size_t size, uint64_t first_us)
{
    for (size_t i = 0U; i < size; i++) {
        uint64_t captured = first_us + i * 100U;
        aCommChannelReceiveAt(handle, data[i], A_STATUS_OK,
                           (uint32_t)captured,
                           (uint32_t)(captured / 1000U));
    }
}

static void captured_timing(uint64_t start_us)
{
    link_t link = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    aCommChannelConfig_t config = configuration(&link);
    aCommChannel_t handle;
    aCommTransport_t transport;
    const uint8_t request[] = {1, 3, 0, 0, 0, 1, 0, 0};
    const uint8_t next[] = {1, 3, 0, 1, 0, 1, 0, 0};
    uint8_t data[8];
    uint64_t first;
    uint32_t quiet;

    now_us = start_us;
    advance(0U);
    assert(aCommChannelInitStatic(&config, &handle) == A_STATUS_OK);
    assert(aCommChannelBindTransport(&handle, &transport) == A_STATUS_OK);
    quiet = handle.frame_gap + handle.character_ticks;

    /* 同帧分块补送时交付间隔很大；同批内两帧靠采集时间区分。 */
    first = now_us + 3000U;
    advance(20000U);
    enter(&link);
    feed_at(&handle, request, 4U, first);
    advance(3000U);
    feed_at(&handle, request + 4U, 4U, first + 400U);
    feed_at(&handle, next, sizeof(next), first + 700U + quiet);
    leave(&link);
    assert(transport.stream.read(
            transport.stream.context, data, sizeof(data),
            A_TIMEOUT_NO_WAIT) == 8);
    assert(memcmp(data, request, sizeof(data)) == 0);
    transport.finish(&handle);
    assert(transport.stream.read(
            transport.stream.context, data, sizeof(data),
            A_TIMEOUT_NO_WAIT) == 8);
    assert(memcmp(data, next, sizeof(data)) == 0);
    transport.finish(&handle);
    assert(transport.stream.read(
            transport.stream.context, data, sizeof(data),
            A_TIMEOUT_NO_WAIT) == -1);
    assert(handle.dropped_frames == 0U);

    /* 补送时无间隔，真实帧内超时或采集错误仍须丢弃整帧。 */
    for (unsigned error = 0U; error < 2U; error++) {
        uint64_t middle;
        first = now_us + 3000U;
        advance(20000U);
        enter(&link);
        feed_at(&handle, request, 4U, first);
        middle = first + 300U + handle.byte_gap + handle.character_ticks;
        if (error != 0U) {
            middle = first + 400U;
            aCommChannelReceiveAt(&handle, 0U, A_STATUS_ERROR,
                               (uint32_t)middle,
                               (uint32_t)(middle / 1000U));
        }
        feed_at(&handle, request + 4U, 4U, middle);
        feed_at(&handle, next, sizeof(next), first + 5000U);
        leave(&link);
        assert(transport.stream.read(
                transport.stream.context, data, sizeof(data),
                                     A_TIMEOUT_NO_WAIT) == 8);
        assert(memcmp(data, next, sizeof(data)) == 0);
        transport.finish(&handle);
        assert(handle.dropped_frames == error + 1U);
    }

    /* 最后一字节尚未达到静默阈值，不因一次批量交付就宣布完整帧。 */
    first = now_us + 3000U;
    advance(3700U);
    enter(&link);
    feed_at(&handle, request, sizeof(request), first);
    leave(&link);
    assert(transport.stream.read(
            transport.stream.context, data, sizeof(data),
            A_TIMEOUT_NO_WAIT) == -1);
    advance(quiet - 1U);
    assert(transport.stream.read(
            transport.stream.context, data, sizeof(data),
            A_TIMEOUT_NO_WAIT) == -1);
    advance(1U);
    assert(transport.stream.read(
            transport.stream.context, data, sizeof(data),
            A_TIMEOUT_NO_WAIT) == 8);
    assert(memcmp(data, request, sizeof(data)) == 0);
    assert(aCommChannelDeInitStatic(&handle) == A_STATUS_OK);
    assert(pthread_mutex_destroy(&link.mutex) == 0);
}

static void timing_defaults(void)
{
    static const struct {
        uint32_t baud;
        uint8_t bits;
        uint32_t character_us, byte_gap_us, frame_gap_us;
    } cases[] = {
        {115200U, 10U, 87U, 750U, 1750U},
        {19201U, 11U, 573U, 750U, 1750U},
        {19200U, 11U, 573U, 860U, 2006U},
        {9600U, 11U, 1146U, 1719U, 4011U},
        {1200U, 10U, 8334U, 12500U, 29167U},
        {1U, 16U, 16000000U, 24000000U, 56000000U},
        {UINT32_MAX, 7U, 1U, 750U, 1750U},
    };
    aModbusRtuTiming_t timing;
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        assert(aModbusRtuTimingDefaults(cases[i].baud, cases[i].bits,
            &timing) == A_STATUS_OK);
        assert(timing.character_us == cases[i].character_us);
        assert(timing.byte_gap_us == cases[i].byte_gap_us);
        assert(timing.frame_gap_us == cases[i].frame_gap_us);
    }
    assert(aModbusRtuTimingDefaults(0U, 10U, &timing) ==
           A_STATUS_INVALID_PARAM);
    assert(aModbusRtuTimingDefaults(115200U, 6U, &timing) ==
           A_STATUS_INVALID_PARAM);
    assert(aModbusRtuTimingDefaults(115200U, 17U, &timing) ==
           A_STATUS_INVALID_PARAM);
    assert(aModbusRtuTimingDefaults(115200U, 10U, NULL) ==
           A_STATUS_INVALID_PARAM);
}

int main(void)
{
    assert(io_timeout.type == A_TIMEOUT_TYPE_RELATIVE);
    assert(io_timeout.milliseconds == 43U);
    assert(transaction_timeout.type == A_TIMEOUT_TYPE_RELATIVE);
    assert(transaction_timeout.milliseconds == 545U);
    assert(static_timing.character_us == 573U);
    assert(static_timing.byte_gap_us == 860U);
    assert(static_timing.frame_gap_us == 2006U);
    timing_defaults();
    failures();
    multi_instance();
    timing(9600U, 11U, 2500U, 6000U);
    timing(1200U, 10U, 20000U, 40000U);
    now_us = UINT32_MAX - 5000U;
    advance(0U);
    timing(115200U, 10U, 800U, 3000U);
    captured_timing(100000U);
    captured_timing((uint64_t)UINT32_MAX - 3400U);
    captured_timing((uint64_t)UINT32_MAX * 1000U - 3400U);
    assert(testAllocations() == 0U);
    puts("Registered RTU multi-instance/captured timing/wrap/lifecycle passed");
    return 0;
}
