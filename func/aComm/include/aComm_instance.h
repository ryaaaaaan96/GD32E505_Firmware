/** @brief 静态实例布局；只在初始化前设置存储指针及容量。 */
#ifndef A_COMM_INSTANCE_H
#define A_COMM_INSTANCE_H

#include "aComm.h"
#include "aAtomic.h"

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
    aCommJobState_t *jobs;
    size_t job_capacity;
    aCommEventState_t *events;
    size_t event_capacity;
    aCommRequest_t *requests;
    size_t request_capacity;
    /* 以下字段均由模块维护，不允许应用修改。 */
    aCommConfig_t config;
    aOSWaitObject_t wake;
    aOSMutex_t queue_mutex;
    size_t head;
    size_t used;
    size_t job_cursor;
    size_t event_cursor;
    unsigned class_cursor;
    aAtomicBool_t active;
    aBool_t ready;
    aBool_t dynamic;
};

#endif
