/** @brief 静态实例布局；只在初始化前设置存储指针及容量。 */
#ifndef A_POLL_INSTANCE_H
#define A_POLL_INSTANCE_H

#include "aPoll.h"
#include "aAtomic.h"

typedef struct {
    size_t step_index;
    uint32_t cycle_started;
    aTimepoint_t delay;
    aBool_t cycle_active;
    aAtomicBool_t triggered;
} aPollJobState_t;

typedef struct {
    aAtomicBool_t pending;
} aPollEventState_t;

struct aPollHandle {
    aPollJobState_t *jobs;
    size_t job_capacity;
    aPollEventState_t *events;
    size_t event_capacity;
    aPollRequest_t *requests;
    size_t request_capacity;
    /* 以下字段均由模块维护，不允许应用修改。 */
    aPollConfig_t config;
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
