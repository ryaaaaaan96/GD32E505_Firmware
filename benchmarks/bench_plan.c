/* 使用生成后的真实点表及协议回调；仅构建计划，不访问任何物理端口。 */
#include "aBus_endpoint.h"
#include "aModbus_mapping.h"
#include "aComm_channel.h"
#include "host_port.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "generated/sig_tables.inc"
#include "generated/FAN_modbus_master.inc"

static aSSize_t no_read(void *context, void *data, size_t size,
    aTimeout_t timeout)
{
    (void)context; (void)data; (void)size; (void)timeout;
    assert(!"构建计划不允许进行 I/O");
    return -1;
}
static aSSize_t no_write(void *context, const void *data, size_t size,
    aTimeout_t timeout)
{
    return no_read(context, (void *)data, size, timeout);
}
static aStatus_t no_control(void *context, aTimeout_t timeout)
{
    (void)context; (void)timeout;
    assert(!"构建计划不允许控制端口");
    return A_STATUS_ERROR;
}
static uint32_t ticks(void *context)
{
    (void)context;
    return benchClock();
}
static void protect(void *context) { (void)context; }

void benchPlanRun(void)
{
    aBusHandle_t *bus = NULL;
    aBusEndpoint_t endpoint;
    aCommChannel_t *channel = NULL;
    aCommChannelConfig_t channel_config;
    aModbusClientMap_t maps[FAN_MODBUS_MASTER_MAP_COUNT];
    aModbusCommConfig_t protocol = {
        .role = AMODBUS_ROLE_CLIENT, .transport_type = AMODBUS_TRANSPORT_RTU,
        .unit_id = 1U, .byte_timeout = A_TIMEOUT_MS(20U),
        .timeout = A_TIMEOUT_MS(500U)
    };
    aModbusComm_t context = {0};
    aCommProtocol_t registration = {
        .config = &protocol, .context = &context,
        .items = maps, .count = FAN_MODBUS_MASTER_MAP_COUNT
    };
    size_t polled = 0U;
    assert(aBusCreate(65533U, tables, sizeof(tables) / sizeof(tables[0]),
        &bus) == A_STATUS_OK);
    assert(aBusEndpointInit(&endpoint, bus) == A_STATUS_OK);
    aCommChannelConfigStructInit(&channel_config);
    channel_config.mode = ACOMM_CHANNEL_GAP;
    channel_config.timing.frame_gap_us = 1750U;
    channel_config.io.ticks_per_second = 1000000000U;
    channel_config.io.ticks = ticks;
    channel_config.io.enter = protect;
    channel_config.io.exit = protect;
    channel_config.stream.read = no_read;
    channel_config.stream.write = no_write;
    channel_config.io.discard_input = no_control;
    channel_config.io.wait_transmit_complete = no_control;
    assert(aCommChannelCreate(&channel_config, &channel) == A_STATUS_OK);
    context.channel = channel;
    context.binding.data = &endpoint.base;
    memcpy(maps, client_maps, sizeof(maps));
    for (size_t i = 0U; i < FAN_MODBUS_MASTER_MAP_COUNT; i++) {
        /* 只测分组和编码；事件注册及通信队列不属于本项的测量范围。 */
        maps[i].comm.subscribe_events = 0U;
        maps[i].comm.publish_events = 0U;
        if (maps[i].comm.poll_time_ms != 0U) polled++;
    }
    for (unsigned split = 0U; split < 2U; split++) {
        aCommPlanConfig_t plan_config;
        size_t frames = 0U;
        uint64_t elapsed = 0U;
        if (split != 0U) {
            for (size_t i = 0U; i < FAN_MODBUS_MASTER_MAP_COUNT; i++)
                maps[i].comm.flags |= ACOMM_MAP_FLAG_BREAK_BEFORE;
        }
        assert(aModbusCommInit(&registration, &plan_config) == A_STATUS_OK);
        size_t base_memory = benchMemoryLive();
        benchMemoryResetPeak();
        for (unsigned sample = 0U; sample < 64U; sample++) {
            aCommPlan_t *plan = NULL;
            uint32_t start = benchClock();
            aStatus_t status = aCommPlanCreate(&plan_config, &plan);
            uint32_t time = benchClock() - start;
            assert(status == A_STATUS_OK);
            elapsed += time;
            frames = aCommPlanFrameCount(plan);
            assert(aCommPlanDestroy(plan) == A_STATUS_OK);
        }
        assert(frames > 0U && frames <= polled);
        if (split != 0U) assert(frames == polled);
        printf("{\"kind\":\"plan\",\"split_all\":%u,\"mapped\":%u,"
            "\"polled\":%zu,\"frames\":%zu,\"samples\":64,"
            "\"mean_create_ns\":%llu,\"peak_plan_bytes\":%zu}\n",
            split, FAN_MODBUS_MASTER_MAP_COUNT, polled, frames,
            (unsigned long long)(elapsed / 64U),
            benchMemoryPeak() - base_memory);
        assert(benchMemoryLive() == base_memory);
        assert(aModbusCommDeInit(&context) == A_STATUS_OK);
    }
    assert(aCommChannelDestroy(channel) == A_STATUS_OK);
    assert(aBusDestroy(bus) == A_STATUS_OK);
}
