/** @brief 静态实例布局；只在初始化前设置存储指针及容量。 */
#ifndef A_COMM_INSTANCE_H
#define A_COMM_INSTANCE_H

#include "aComm.h"
#include "aComm_channel.h"
#include "aAtomic.h"

/** 静态资源按启用表项数给上限，动态模式按实际帧数分配。 */
typedef struct {
    size_t first;
    size_t count;
    uint32_t poll_time_ms;
    aCommPrepared_t request;
} aCommPlanFrame_t;
struct aCommPlan {
    aCommPlanFrame_t *frames;
    size_t *indices;
    size_t capacity;
    size_t index_capacity;
    aCommPlanConfig_t config;
    size_t count;
    aBool_t ready;
    aBool_t dynamic;
};
typedef struct {
    aCommJobConfig_t config;
    aCommPlan_t *plan;
    size_t first_frame;
} aCommTableJob_t;

typedef struct {
    size_t step_index; /**< 下一步下标；等于 step_count 表示单轮作业已完成。 */
    uint16_t retries; /**< 当前步骤已使用的额外尝试次数。 */
    uint32_t cycle_started;
    aTimepoint_t delay;
    aBool_t cycle_active;
    aAtomicBool_t triggered;
} aCommJobState_t;

typedef struct {
    aAtomicBool_t pending;
} aCommEventState_t;

struct aCommHandle {
    aCommPlan_t plan; /**< table 模式的帧、下标资源。 */
    aCommTableJob_t *table_jobs;
    size_t table_capacity; /**< 周期分组容量，静态时可按表项数给上限。 */
    aCommJobState_t *jobs;
    size_t job_capacity;
    aCommEventState_t *events;
    size_t event_capacity;
    aCommRequest_t *requests;
    size_t request_capacity;
    /* 以下字段均由模块维护，不允许应用修改。 */
    aCommConfig_t config;
    aCommJobConfig_t protocol_job; /**< 无采集表的注册协议共用此服务作业。 */
    aOSWaitObject_t wake;
    aOSMutex_t queue_mutex;
    size_t head;
    size_t used;
    size_t job_cursor;
    size_t event_cursor;
    unsigned class_cursor;
    aAtomicBool_t active;
    aBool_t ready;
    aBool_t stopping; /**< 释放回调失败后只允许继续释放，禁止再次处理。 */
    aBool_t dynamic;
};

/* -------------------- 通道缓冲与时序状态 -------------------- */

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

/* -------------------- 数据绑定与订阅状态 -------------------- */

typedef struct {
    void *native; /**< 数据端点原生节点；初始化前由宿主提供。 */
    const aCommEntry_t *entry;
    aCommBinding_t *owner;
    aAtomicBool_t busy;
    aCommNotifyInput_t input;
    aCommRequest_t work;
} aCommBindingSubscription_t;
struct aCommBinding {
    aCommBindingSubscription_t *subscriptions;
    size_t subscription_capacity;
    size_t subscription_count;
    aCommBindingConfig_t config;
    aCommBindingEvents_t events;
    aBool_t ready;
    _Alignas(max_align_t) uint8_t snapshot[ACOMM_VALUE_CAPACITY];
};

#endif
