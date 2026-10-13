/** @brief 静态通道存储；全部字段私有，活动期间禁止复制或移动。 */
#ifndef A_COMM_CHANNEL_INSTANCE_H
#define A_COMM_CHANNEL_INSTANCE_H

#include "aComm_channel.h"

/* 全工程统一配置；不代表任何协议的报文上限。 */
#ifndef ACOMM_CHANNEL_FRAME_SIZE
#define ACOMM_CHANNEL_FRAME_SIZE 256U
#endif
#ifndef ACOMM_CHANNEL_FRAME_COUNT
#define ACOMM_CHANNEL_FRAME_COUNT 3U
#endif
#if ACOMM_CHANNEL_FRAME_SIZE < 1 || ACOMM_CHANNEL_FRAME_COUNT < 1
#error "aComm channel frame storage must not be empty"
#endif

typedef struct {
    uint8_t data[ACOMM_CHANNEL_FRAME_SIZE];
    size_t size;
} aCommChannelFrame_t;

struct aCommChannel {
    aCommChannelConfig_t config;
    aCommChannelFrame_t frames[ACOMM_CHANNEL_FRAME_COUNT];
    size_t head, tail, count, building;
    uint32_t last_rx_ticks, last_rx_ms;
    uint32_t last_tx_ticks, last_tx_ms;
    uint32_t frame_gap, byte_gap, character_ticks, idle_ms;
    uint32_t dropped_frames;
    uint8_t frame[ACOMM_CHANNEL_FRAME_SIZE];
    size_t frame_size, frame_position;
    aBool_t invalid_frame, frame_ready, tx_pending;
    aBool_t ready, dynamic;
};

#endif
