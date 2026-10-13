/* 不链接任何协议栈，验证任意字节帧、流式传输及注册回调。 */
#include "aComm_channel_instance.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void testSetTime(uint32_t ms);
unsigned testAllocations(void);
static uint32_t now_us;
static unsigned locked, checks, writes, flushes, waits;
static aBool_t eof;

void aOSDelayMs(uint32_t ms)
{
    now_us += ms * 1000U;
    testSetTime(now_us / 1000U);
}
static uint32_t ticks(void *context) { (void)context; return now_us; }
static void enter(void *context) { (void)context; locked++; }
static void leave(void *context) { (void)context; locked--; }
static aStatus_t wait_tx(void *context, aTimeout_t timeout)
{
    (void)context;
    assert(aTimeoutIsValid(timeout));
    waits++;
    return A_STATUS_OK;
}
static aSSize_t read_bytes(void *context, void *data, size_t size,
    aTimeout_t timeout)
{
    assert(context == &eof && aTimeoutIsValid(timeout));
    if (eof) return 0;
    assert(size != 0U);
    ((uint8_t *)data)[0] = 0xA5U;
    return 1; /* 普通流允许半包，通道不自行补齐。 */
}
static aSSize_t write_bytes(void *context, const void *data, size_t size,
    aTimeout_t timeout)
{
    assert(context == &eof && data != NULL && aTimeoutIsValid(timeout));
    writes++;
    return size > 2U ? 2 : (aSSize_t)size;
}
static aStatus_t flush_bytes(void *context, aTimeout_t timeout)
{
    assert(context == &eof && aTimeoutIsValid(timeout));
    flushes++;
    return A_STATUS_OK;
}
static aStatus_t check_frame(void *context, const void *data, size_t size)
{
    assert(context == &checks && locked == 0U && size != 0U);
    checks++;
    if (((const uint8_t *)data)[0] == 0xFFU) return A_STATUS_NOT_FOUND;
    return ((const uint8_t *)data)[0] == 0xEEU ?
        A_STATUS_ERROR : A_STATUS_OK;
}
static aCommChannel_t *create(const aCommChannelConfig_t *config)
{
#if ACOMM_DYNAMIC_ENABLE
    aCommChannel_t *handle = NULL;
    assert(aCommChannelCreate(config, &handle) == A_STATUS_OK);
    return handle;
#else
    static aCommChannel_t storage;
    assert(aCommChannelInitStatic(config, &storage) == A_STATUS_OK);
    return &storage;
#endif
}
static void destroy(aCommChannel_t *handle)
{
#if ACOMM_DYNAMIC_ENABLE
    assert(aCommChannelDestroy(handle) == A_STATUS_OK);
#else
    assert(aCommChannelDeInitStatic(handle) == A_STATUS_OK);
#endif
}
static void feed(aCommChannel_t *handle, uint8_t value, size_t size)
{
    for (size_t i = 0U; i < size; i++) {
        aCommChannelReceive(handle, value, A_STATUS_OK);
    }
    aOSDelayMs(2U);
}
static void stream_test(void)
{
    aCommChannelConfig_t config;
    aCommTransport_t transport;
    uint8_t data[4];
    aCommChannelConfigStructInit(&config);
    config.stream = (aStream_t){
        .context = &eof, .read = read_bytes, .write = write_bytes,
        .flush = flush_bytes
    };
    aCommChannel_t *handle = create(&config);
    assert(aCommChannelBindTransport(handle, &transport) == A_STATUS_OK);
    assert(transport.stream.read(transport.stream.context, data,
        sizeof(data), A_TIMEOUT_NO_WAIT) == 1);
    assert(data[0] == 0xA5U);
    assert(transport.stream.write(transport.stream.context, data,
        sizeof(data), A_TIMEOUT_NO_WAIT) == 2);
    assert(transport.stream.flush(transport.stream.context,
        A_TIMEOUT_NO_WAIT) == A_STATUS_OK);
    assert(writes == 1U && flushes == 1U);
    /* 未提供可清空流的能力，不能默默丢掉后续 TCP/其他流报文。 */
    assert(transport.discard_input == NULL);
    assert(transport.prepare_frame == NULL);
    assert(transport.wait_transmit_complete == NULL);
    assert(transport.finish == NULL);
    aCommChannelReceive(handle, 0xEEU, A_STATUS_OK);
    eof = A_TRUE;
    assert(transport.stream.read(transport.stream.context, data,
        sizeof(data), A_TIMEOUT_NO_WAIT) == 0);
    destroy(handle);
}
static void framing_test(void)
{
    aCommChannelConfig_t config;
    aCommTransport_t transport;
    uint8_t data[ACOMM_CHANNEL_FRAME_SIZE + 1U];
    aCommChannelConfigStructInit(&config);
    config.mode = ACOMM_CHANNEL_GAP;
    config.stream.context = &eof;
    config.stream.write = write_bytes;
    config.io = (aCommChannelIo_t){
        .ticks_per_second = 1000000U, .ticks = ticks,
        .enter = enter, .exit = leave, .wait_transmit_complete = wait_tx
    };
    /* 任意时间策略；与具体协议、波特率、功能码无关。 */
    config.timing.frame_gap_us = 1000U;
    config.frame_check = check_frame;
    config.frame_context = &checks;
    aCommChannel_t *handle = create(&config);
    assert(aCommChannelBindTransport(handle, &transport) == A_STATUS_OK);
    aOSDelayMs(2U);
    feed(handle, 0x7EU, 1U); /* 一字节帧同样有效。 */
    assert(checks == 0U);
    assert(transport.stream.read(transport.stream.context, data,
        sizeof(data), A_TIMEOUT_NO_WAIT) == 1);
    assert(data[0] == 0x7EU && checks == 1U);
    assert(transport.stream.read(transport.stream.context, data,
        sizeof(data), A_TIMEOUT_NO_WAIT) == -1);
    assert(aOSGetErrno() == A_EIO);
    transport.finish(transport.context);
    feed(handle, 0xFFU, 1U);
    feed(handle, 0x42U, 3U);
    assert(transport.stream.read(transport.stream.context, data,
        sizeof(data), A_TIMEOUT_MS(5U)) == 3);
    assert(data[0] == 0x42U && checks == 3U);
    transport.finish(transport.context);
    feed(handle, 0xEEU, 1U);
    assert(transport.stream.read(transport.stream.context, data,
        sizeof(data), A_TIMEOUT_NO_WAIT) == -1);
    assert(aOSGetErrno() == A_EIO);
    feed(handle, 0x11U, sizeof(data)); /* 超容量整帧丢弃，不截短交付。 */
    assert(transport.stream.read(transport.stream.context, data,
        sizeof(data), A_TIMEOUT_NO_WAIT) == -1);
    assert(aOSGetErrno() == A_EAGAIN);
    /* 队列满丢弃新帧，已排队帧内容不变。 */
    for (size_t i = 0U; i < ACOMM_CHANNEL_FRAME_COUNT + 1U; i++) {
        feed(handle, (uint8_t)(i + 1U), 1U);
    }
    for (size_t i = 0U; i < ACOMM_CHANNEL_FRAME_COUNT; i++) {
        assert(transport.stream.read(transport.stream.context, data,
            sizeof(data), A_TIMEOUT_NO_WAIT) == 1);
        assert(data[0] == i + 1U);
        transport.finish(transport.context);
    }
    assert(transport.stream.read(transport.stream.context, data,
        sizeof(data), A_TIMEOUT_NO_WAIT) == -1);
    assert(transport.prepare_frame(transport.context,
        A_TIMEOUT_MS(5U)) == A_STATUS_OK);
    assert(waits != 0U && locked == 0U);
    destroy(handle);
}

static uint32_t captured_ticks;
static uint32_t test_ticks(void *context)
{
    (void)context;
    return captured_ticks;
}

static aStatus_t config_status(const aCommChannelConfig_t *config)
{
#if ACOMM_DYNAMIC_ENABLE
    aCommChannel_t *handle = (void *)1;
    aStatus_t status = aCommChannelCreate(config, &handle);
    if (status == A_STATUS_OK) destroy(handle);
    else assert(handle == NULL);
#else
    aCommChannel_t storage;
    aStatus_t status = aCommChannelInitStatic(config, &storage);
    if (status == A_STATUS_OK) destroy(&storage);
#endif
    return status;
}

static void timing_conversion(void)
{
    aCommChannelConfig_t config;
    aCommTransport_t transport;
    uint8_t data;
    aCommChannelConfigStructInit(&config);
    config.mode = ACOMM_CHANNEL_GAP;
    config.stream.context = &eof;
    config.stream.write = write_bytes;
    config.io = (aCommChannelIo_t){
        .ticks_per_second = 32768U, .ticks = test_ticks,
        .enter = enter, .exit = leave, .wait_transmit_complete = wait_tx
    };
    config.timing = (aCommTiming_t){
        .character_us = 87U, .byte_gap_us = 750U, .frame_gap_us = 1750U
    };
    now_us = captured_ticks = 0U;
    testSetTime(0U);
    aCommChannel_t *handle = create(&config);
    assert(handle->character_ticks == 3U);
    assert(handle->byte_gap == 25U && handle->frame_gap == 58U);
    assert(aCommChannelBindTransport(handle, &transport) == A_STATUS_OK);
    aCommChannelReceive(handle, 0x42U, A_STATUS_OK);
    /* 非整数换算必须向上取整，阈值前不可提前交付完整帧。 */
    captured_ticks = 60U;
    assert(transport.stream.read(transport.stream.context, &data, 1U,
        A_TIMEOUT_NO_WAIT) == -1);
    assert(aOSGetErrno() == A_EAGAIN);
    captured_ticks = 61U;
    assert(transport.stream.read(transport.stream.context, &data, 1U,
        A_TIMEOUT_NO_WAIT) == 1);
    assert(data == 0x42U);
    destroy(handle);

    /* 应用值不被默认值覆盖；零明确关闭帧内检查及字符补偿。 */
    config.timing = (aCommTiming_t){.frame_gap_us = 2222U};
    handle = create(&config);
    assert(handle->frame_gap == 73U && handle->byte_gap == 0U);
    assert(handle->character_ticks == 0U);
    destroy(handle);

    config.io.ticks_per_second = 180000000U;
    config.timing = (aCommTiming_t){
        .character_us = 87U, .byte_gap_us = 750U, .frame_gap_us = 1750U
    };
    handle = create(&config);
    assert(handle->character_ticks == 15660U);
    assert(handle->byte_gap == 135000U && handle->frame_gap == 315000U);
    destroy(handle);

    config.io.ticks_per_second = 1U;
    config.timing = (aCommTiming_t){.frame_gap_us = 1U};
    handle = create(&config);
    assert(handle->frame_gap == 1U); /* 正时间不能被截断为零。 */
    destroy(handle);
    config.timing.frame_gap_us = 0U;
    assert(config_status(&config) == A_STATUS_INVALID_PARAM);
    config.timing = (aCommTiming_t){.byte_gap_us = 2U, .frame_gap_us = 1U};
    assert(config_status(&config) == A_STATUS_INVALID_PARAM);
    config.timing = (aCommTiming_t){.frame_gap_us = 1U};
    config.io.ticks_per_second = 0U;
    assert(config_status(&config) == A_STATUS_INVALID_PARAM);

    config.io.ticks_per_second = UINT32_MAX;
    config.timing.frame_gap_us = UINT32_MAX;
    assert(config_status(&config) == A_STATUS_UNSUPPORTED);
    config.io.ticks_per_second = 1000000U;
    config.timing.frame_gap_us = UINT32_MAX / 2U;
    assert(config_status(&config) == A_STATUS_OK);
    config.timing.character_us = 1U;
    assert(config_status(&config) == A_STATUS_UNSUPPORTED);
}
int main(void)
{
    stream_test();
    framing_test();
    timing_conversion();
    assert(testAllocations() == 0U);
    puts("Generic channel: stream/EOF/framing/callbacks/us conversion passed");
    return 0;
}
