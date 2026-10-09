/* 不链接 aDrv/aDev，验证通用分帧、多实例、低波特率及生命周期。 */
#include "aModbus_rtu_instance.h"
#include "aOS.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

void testSetTime(uint32_t ms);
unsigned testAllocations(void);
void testFailAllocation(aBool_t fail);

static uint64_t now_us;
typedef struct {
    pthread_mutex_t mutex;
    unsigned clears, writes, waits;
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
static link_t *links[2];
static aModbusRtuHandle_t *bound[2];
#define DEFINE_STREAM(index) \
static aSSize_t output##index(const void *data, size_t size, aTimeout_t t) \
{ return write_bytes(links[index], data, size, t); } \
static aSSize_t read##index(void *data, size_t size, aTimeout_t t) \
{ return aModbusRtuRead(bound[index], data, size, t); } \
static aSSize_t write##index(const void *data, size_t size, aTimeout_t t) \
{ return aModbusRtuWrite(bound[index], data, size, t); }
DEFINE_STREAM(0)
DEFINE_STREAM(1)

static aStatus_t bind_transport(aModbusRtuHandle_t *handle,
    aModbusTransport_t *transport, size_t index)
{
    bound[index] = handle;
    aModbusTransportStructInit(transport);
    transport->stream.read = index == 0U ? read0 : read1;
    transport->stream.write = index == 0U ? write0 : write1;
    return aModbusRtuBindTransport(handle, transport);
}

static aModbusRtuConfig_t configuration(link_t *link, size_t index)
{
    aModbusRtuConfig_t config;
    aModbusRtuConfigStructInit(&config);
    config.io.context = link;
    config.io.ticks_per_second = 1000000U;
    config.io.ticks = ticks;
    config.io.enter = enter;
    config.io.exit = leave;
    links[index] = link;
    config.output.write = index == 0U ? output0 : output1;
    config.io.wait_transmit_complete = wait_transmit;
    config.io.clear_error = clear_error;
    return config;
}
static void feed(aModbusRtuHandle_t *handle, const uint8_t *data, size_t size)
{
    const aModbusRtuIo_t *io = &handle->config.io;
    io->enter(io->context);
    for (size_t i = 0U; i < size; i++) {
        aModbusRtuReceive(handle, data[i], A_STATUS_OK);
    }
    io->exit(io->context);
}

static void multi_instance(void)
{
    link_t first = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    link_t second = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    aModbusRtuConfig_t config = configuration(&first, 0U);
    aModbusRtuHandle_t storage, *other;
    aModbusTransport_t one, two;
    uint8_t data[20];
    const uint8_t request[] = {1, 3, 0, 0, 0, 1, 0, 0};
    const uint8_t response[] = {2, 3, 2, 0, 9, 0, 0};
    unsigned baseline = testAllocations();

    assert(aModbusRtuInitStatic(&config, &storage) == A_STATUS_OK);
    config = configuration(&second, 1U);
    config.role = AMODBUS_ROLE_CLIENT;
    assert(aModbusRtuCreate(&config, &other) == A_STATUS_OK);
    assert(bind_transport(&storage, &one, 0U) == A_STATUS_OK);
    assert(bind_transport(other, &two, 1U) == A_STATUS_OK);
    assert(one.context != two.context && one.finish != NULL);
    advance(3000U);
    feed(&storage, request, sizeof(request));
    feed(other, response, sizeof(response));
    advance(3000U);
    feed(&storage, request, sizeof(request));
    advance(3000U);
    assert(one.stream.read(data, 3U, A_TIMEOUT_NO_WAIT) == 3);
    assert(memcmp(data, request, 3U) == 0);
    assert(one.stream.read(data, sizeof(data), A_TIMEOUT_NO_WAIT) == 5);
    assert(memcmp(data, request + 3U, 5U) == 0);
    assert(one.stream.read(data, sizeof(data), A_TIMEOUT_NO_WAIT) == 0);
    one.finish(one.context);
    assert(one.stream.read(data, sizeof(data), A_TIMEOUT_NO_WAIT) == 8);
    assert(memcmp(data, request, sizeof(request)) == 0);
    assert(two.stream.read(data, sizeof(data), A_TIMEOUT_NO_WAIT) == 7);
    assert(memcmp(data, response, sizeof(response)) == 0);
    one.finish(one.context);
    two.finish(two.context);
    assert(one.discard_input(one.context, A_TIMEOUT_MS(5U)) == A_STATUS_OK);
    assert(first.clears == 1U && second.clears == 0U);
    assert(one.prepare_frame(one.context, A_TIMEOUT_MS(5U)) == A_STATUS_OK);
    assert(one.stream.write(request, sizeof(request),
                     A_TIMEOUT_MS(5U)) == 8);
    assert(one.wait_transmit_complete(one.context,
                                      A_TIMEOUT_MS(5U)) == A_STATUS_OK);
    assert(first.writes == 1U && second.writes == 0U);
    assert(testAllocations() == baseline + 1U);
    assert(aModbusRtuDestroy(&storage) == A_STATUS_INVALID_PARAM);
    assert(aModbusRtuDeInitStatic(other) == A_STATUS_INVALID_PARAM);
    assert(aModbusRtuDeInitStatic(&storage) == A_STATUS_OK);
    assert(bind_transport(&storage, &one, 0U) == A_STATUS_NOT_READY);
    assert(aModbusRtuDestroy(other) == A_STATUS_OK);
    assert(testAllocations() == baseline);
    assert(pthread_mutex_destroy(&first.mutex) == 0);
    assert(pthread_mutex_destroy(&second.mutex) == 0);
}

static void timing(uint32_t baud, uint8_t bits, uint32_t gap,
                   uint32_t quiet)
{
    link_t link = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    aModbusRtuConfig_t config = configuration(&link, 0U);
    aModbusRtuHandle_t handle;
    aModbusTransport_t transport;
    uint8_t data[8];
    const uint8_t request[] = {7, 3, 0, 0, 0, 1, 0, 0};
    config.baud_rate = baud;
    config.character_bits = bits;
    config.unit_id = 7U;
    assert(aModbusRtuInitStatic(&config, &handle) == A_STATUS_OK);
    assert(bind_transport(&handle, &transport, 0U) == A_STATUS_OK);
    advance(quiet);
    feed(&handle, request, 4U);
    advance(gap);
    feed(&handle, request + 4U, 4U);
    advance(quiet);
    assert(transport.stream.read(data, sizeof(data),
                          A_TIMEOUT_NO_WAIT) == 8);
    assert(memcmp(data, request, sizeof(data)) == 0);
    transport.finish(transport.context);
    /* 超出帧内间隔，但没有到帧间静默，整帧必须丢弃。 */
    feed(&handle, request, 4U);
    advance(handle.byte_gap + handle.character_ticks + 100U);
    feed(&handle, request + 4U, 4U);
    advance(quiet);
    assert(transport.stream.read(data, sizeof(data),
                          A_TIMEOUT_MS(1U)) == -1);
    assert(handle.dropped_frames == 1U);
    assert(aModbusRtuDeInitStatic(&handle) == A_STATUS_OK);
    assert(pthread_mutex_destroy(&link.mutex) == 0);
}

static void failures(void)
{
    link_t link = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    aModbusRtuConfig_t config = configuration(&link, 0U);
    aModbusRtuHandle_t handle, *created = (void *)1;
    assert(aModbusRtuCreate(NULL, &created) == A_STATUS_INVALID_PARAM);
    assert(created == NULL);
    config.baud_rate = 0U;
    assert(aModbusRtuInitStatic(&config, &handle) == A_STATUS_INVALID_PARAM);
    config = configuration(&link, 0U);
    config.io.ticks = NULL;
    assert(aModbusRtuInitStatic(&config, &handle) == A_STATUS_INVALID_PARAM);
    config = configuration(&link, 0U);
    config.baud_rate = 1U;
    config.io.ticks_per_second = UINT32_MAX;
    assert(aModbusRtuInitStatic(&config, &handle) == A_STATUS_UNSUPPORTED);
    config = configuration(&link, 0U);
    testFailAllocation(A_TRUE);
    assert(aModbusRtuCreate(&config, &created) == A_STATUS_NO_MEMORY);
    assert(created == NULL);
    testFailAllocation(A_FALSE);
    assert(pthread_mutex_destroy(&link.mutex) == 0);
}

int main(void)
{
    failures();
    multi_instance();
    timing(9600U, 11U, 2500U, 6000U);
    timing(1200U, 10U, 20000U, 40000U);
    now_us = UINT32_MAX - 5000U;
    advance(0U);
    timing(115200U, 10U, 800U, 3000U);
    assert(testAllocations() == 0U);
    puts("Generic RTU multi-instance/timing/wrap/lifecycle passed");
    return 0;
}
