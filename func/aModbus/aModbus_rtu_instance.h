/** @brief 静态 RTU 存储；全部字段私有，活动期间禁止复制或移动。 */
#ifndef A_MODBUS_RTU_INSTANCE_H
#define A_MODBUS_RTU_INSTANCE_H

#include "aModbus_rtu.h"

#define AMODBUS_RTU_FRAME_SIZE 256U
#define AMODBUS_RTU_FRAME_COUNT 3U

typedef struct {
    uint8_t data[AMODBUS_RTU_FRAME_SIZE];
    size_t size;
} aModbusRtuFrame_t;

struct aModbusRtuHandle {
    aModbusRtuConfig_t config;
    aModbusRtuFrame_t frames[AMODBUS_RTU_FRAME_COUNT];
    size_t head, tail, count, building;
    uint32_t last_rx_ticks, last_rx_ms;
    uint32_t last_tx_ticks, last_tx_ms;
    uint32_t frame_gap, byte_gap, character_ticks, idle_ms;
    uint32_t dropped_frames;
    uint8_t frame[AMODBUS_RTU_FRAME_SIZE];
    size_t frame_size, frame_position;
    aBool_t invalid_frame, frame_ready, tx_pending;
    aBool_t ready, dynamic;
};

#endif
