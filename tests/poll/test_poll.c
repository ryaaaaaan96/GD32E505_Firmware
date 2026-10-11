#include "aPoll_instance.h"
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>

void testSetTime(uint32_t ms);
void testAdvanceTime(uint32_t ms);
unsigned testAllocations(void);
void testFailAllocation(aBool_t fail);
void testFailWaitCreate(aBool_t fail);
void aOSDelayMs(uint32_t ms) { testAdvanceTime(ms); }

static aPollHandle_t *poll;
static unsigned executed, completed, events;
static aBool_t advance = A_TRUE, repost;
static aStatus_t execution_status = A_STATUS_OK;
static size_t step_seen;
#if APOLL_STATIC_ENABLE
static aPollJobState_t jobs[3];
static aPollEventState_t event_states[40];
static aPollRequest_t requests[2];
static aPollHandle_t instance = {
    .jobs = jobs, .job_capacity = 3U,
    .events = event_states, .event_capacity = 40U,
    .requests = requests, .request_capacity = 2U
};
#endif

static void execute(void *context, size_t step, aPollStepResult_t *result)
{
    aPollProcessResult_t nested;
    (void)context;
    executed++;
    step_seen = step;
    assert(aPollProcess(poll, &nested) == A_STATUS_BUSY);
    assert(aPollWait(poll, A_TIMEOUT_NO_WAIT) == A_STATUS_BUSY);
    result->status = execution_status;
    result->advance = advance;
}

static void event(void *context)
{
    (void)context;
    events++;
    if (repost) {
        repost = A_FALSE;
        assert(aPollEventPost(poll, 0U) == A_STATUS_OK);
    }
}

static void complete(void *context, const aPollStepResult_t *result)
{
    (void)context;
    assert(result->status == execution_status);
    completed++;
}

static void create(const aPollConfig_t *config)
{
#if APOLL_STATIC_ENABLE
    assert(aPollInitStatic(config, &instance) == A_STATUS_OK);
    poll = &instance;
    assert(aPollInitStatic(config, &instance) == A_STATUS_BUSY);
#else
    assert(aPollCreate(config, &poll) == A_STATUS_OK);
#endif
}

static aStatus_t destroy(void)
{
#if APOLL_STATIC_ENABLE
    return aPollDeInitStatic(poll);
#else
    return aPollDestroy(poll);
#endif
}

static aPollProcessResult_t process(void)
{
    aPollProcessResult_t result;
    assert(aPollProcess(poll, &result) == A_STATUS_OK);
    return result;
}

static void periodic(void)
{
    aPollJobConfig_t job;
    aPollConfig_t config;
    aPollProcessResult_t result;
    aPollJobConfigStructInit(&job);
    aPollConfigStructInit(&config);
    job.execute = execute;
    job.step_count = 2U;
    job.poll_time_ms = 20U;
    job.interval_ms = 1U;
    config.jobs = &job;
    config.job_count = 1U;
    testSetTime(UINT32_MAX - 8U);
    create(&config);
    result = process();
    assert(result.type == APOLL_WORK_JOB && step_seen == 0U);
    assert(result.wait.milliseconds == 1U);
    assert(aPollWait(poll, result.wait) == A_STATUS_TIMEOUT);
    result = process();
    assert(result.type == APOLL_WORK_JOB && step_seen == 1U);
    assert(result.wait.milliseconds == 19U);
    assert(aPollWait(poll, result.wait) == A_STATUS_TIMEOUT);
    result = process();
    assert(step_seen == 0U && result.type == APOLL_WORK_JOB);
    /* 未尝试不推进，且必须按错误间隔让出执行机会。 */
    testAdvanceTime(1U);
    advance = A_FALSE;
    execution_status = A_STATUS_BUSY;
    result = process();
    assert(step_seen == 1U && result.wait.milliseconds == 5U);
    testAdvanceTime(5U);
    advance = A_TRUE;
    execution_status = A_STATUS_TIMEOUT;
    result = process();
    assert(step_seen == 1U && result.execution.advance);
    assert(result.execution.status == A_STATUS_TIMEOUT);
    /* 已经超出周期，不为错过的轮次补跑；从本次起点开始新轮。 */
    testAdvanceTime(100U);
    execution_status = A_STATUS_OK;
    result = process();
    assert(step_seen == 0U);
    testAdvanceTime(1U);
    result = process();
    assert(step_seen == 1U && result.wait.milliseconds == 19U);
    /* 主动触发提前开始，但仍遵守成功后的 1 ms 等待。 */
    assert(aPollJobTrigger(poll, 0U) == A_STATUS_OK);
    assert(aPollWait(poll, result.wait) == A_STATUS_OK);
    result = process();
    assert(result.type == APOLL_WORK_NONE && result.wait.milliseconds == 1U);
    testAdvanceTime(1U);
    result = process();
    assert(result.type == APOLL_WORK_JOB && step_seen == 0U);
    assert(destroy() == A_STATUS_OK);
}

static void *poster(void *context)
{
    size_t id = *(const size_t *)context;
    for (unsigned i = 0U; i < 10000U; i++) {
        assert(aPollEventPost(poll, id) == A_STATUS_OK);
    }
    return NULL;
}

static void notifications(void)
{
    aPollConfig_t config;
    aPollEventConfig_t table[40];
    aPollProcessResult_t result;
    pthread_t threads[2];
    const size_t ids[] = {0U, 39U};
    aPollConfigStructInit(&config);
    for (size_t i = 0U; i < 40U; i++) {
        aPollEventConfigStructInit(&table[i]);
        table[i].function = event;
    }
    config.events = table;
    config.event_count = 40U;
    create(&config);
    result = process();
    assert(result.type == APOLL_WORK_NONE);
    assert(result.wait.type == A_TIMEOUT_TYPE_FOREVER);
    /* 事件到达位于 Process 与 Wait 之间，通知必须留存。 */
    assert(aPollEventPost(poll, 0U) == A_STATUS_OK);
    assert(aPollEventPost(poll, 0U) == A_STATUS_OK);
    assert(aPollWait(poll, result.wait) == A_STATUS_OK);
    events = 0U;
    repost = A_TRUE;
    result = process();
    assert(events == 1U && result.wait.milliseconds == 0U);
    result = process();
    assert(events == 2U && result.type == APOLL_WORK_EVENT);
    result = process();
    assert(result.type == APOLL_WORK_NONE);
    for (size_t i = 0U; i < 2U; i++) {
        assert(pthread_create(&threads[i], NULL, poster, (void *)&ids[i]) == 0);
    }
    for (size_t i = 0U; i < 2U; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
    assert(process().type == APOLL_WORK_EVENT);
    assert(process().type == APOLL_WORK_EVENT);
    assert(process().type == APOLL_WORK_NONE);
    assert(events == 4U);
    assert(aPollEventPost(poll, 40U) == A_STATUS_NOT_FOUND);
    assert(destroy() == A_STATUS_OK);
}

static void fairness(void)
{
    aPollConfig_t config;
    aPollJobConfig_t job;
    aPollEventConfig_t ev;
    aPollRequest_t request;
    aPollProcessResult_t result;
    unsigned allocations;
    aPollConfigStructInit(&config);
    aPollJobConfigStructInit(&job);
    aPollEventConfigStructInit(&ev);
    aPollRequestStructInit(&request);
    job.execute = execute;
    ev.function = event;
    request.execute = execute;
    request.complete = complete;
    config.jobs = &job;
    config.job_count = 1U;
    config.events = &ev;
    config.event_count = 1U;
    config.request_capacity = 2U;
    create(&config);
    allocations = testAllocations();
    assert(aPollSubmit(poll, &request) == A_STATUS_OK);
    assert(aPollSubmit(poll, &request) == A_STATUS_OK);
    assert(aPollSubmit(poll, &request) == A_STATUS_BUSY);
    assert(destroy() == A_STATUS_BUSY);
    executed = events = completed = 0U;
    for (unsigned i = 0U; i < 30U; i++) {
        assert(aPollEventPost(poll, 0U) == A_STATUS_OK);
        result = process();
        assert(result.type == (aPollWorkType_t)(1U + i % 3U));
        if (result.type == APOLL_WORK_REQUEST) {
            assert(aPollSubmit(poll, &request) == A_STATUS_OK);
        }
    }
    assert(events == 10U && completed == 10U && executed == 20U);
    assert(testAllocations() == allocations);
    while (completed < 12U) (void)process();
    assert(aOSMutexLock(poll->queue_mutex, A_TIMEOUT_NO_WAIT) == A_STATUS_OK);
    poll->class_cursor = 2U;
    result = process();
    assert(result.type == APOLL_WORK_JOB && result.wait.milliseconds == 0U);
    assert(aOSMutexUnlock(poll->queue_mutex) == A_STATUS_OK);
    assert(destroy() == A_STATUS_OK);
}

static unsigned fifo_count;
static unsigned fifo_values[] = {0U, 1U, 2U};

static void fifo_execute(void *context, size_t step, aPollStepResult_t *result)
{
    aPollRequest_t followup;
    unsigned value = *(const unsigned *)context;
    assert(step == 0U && value == fifo_count++);
    if (value == 0U) {
        /* 消费已释放队列锁，回调可以利用空槽再提交一条。 */
        aPollRequestStructInit(&followup);
        followup.execute = fifo_execute;
        followup.context = &fifo_values[2];
        assert(aPollSubmit(poll, &followup) == A_STATUS_OK);
    }
    result->status = A_STATUS_ERROR;
    result->advance = A_FALSE;
}

static void fifo(void)
{
    aPollConfig_t config;
    aPollRequest_t request;
    aPollConfigStructInit(&config);
    aPollRequestStructInit(&request);
    config.request_capacity = 2U;
    create(&config);
    request.execute = fifo_execute;
    request.context = &fifo_values[0];
    assert(aPollSubmit(poll, &request) == A_STATUS_OK);
    request.context = &fifo_values[1];
    assert(aPollSubmit(poll, &request) == A_STATUS_OK);
    /* 入队复制描述；调用者改写局部描述不改变已排队请求。 */
    request.execute = NULL;
    for (unsigned i = 0U; i < 3U; i++) {
        aPollProcessResult_t result = process();
        assert(result.type == APOLL_WORK_REQUEST);
        assert(result.execution.status == A_STATUS_ERROR);
    }
    assert(fifo_count == 3U && process().type == APOLL_WORK_NONE);
    assert(destroy() == A_STATUS_OK);
}

static void failures(void)
{
    aPollConfig_t config;
    unsigned baseline = testAllocations();
    aPollConfigStructInit(&config);
#if APOLL_DYNAMIC_ENABLE
    aPollHandle_t *allocated;
    testFailAllocation(A_TRUE);
    assert(aPollCreate(&config, &allocated) == A_STATUS_NO_MEMORY);
    assert(allocated == NULL);
    testFailAllocation(A_FALSE);
    testFailWaitCreate(A_TRUE);
    assert(aPollCreate(&config, &allocated) == A_STATUS_NO_MEMORY);
    assert(testAllocations() == baseline);
    testFailWaitCreate(A_FALSE);
    assert(aPollCreate(&config, &allocated) == A_STATUS_OK);
    assert(aPollDestroy(allocated) == A_STATUS_OK);
    config.request_capacity = SIZE_MAX;
    assert(aPollCreate(&config, &allocated) == A_STATUS_INVALID_PARAM);
#endif
#if APOLL_STATIC_ENABLE
    aPollConfigStructInit(&config);
    config.request_capacity = 3U;
    assert(aPollInitStatic(&config, &instance) == A_STATUS_INVALID_PARAM);
    config.request_capacity = 0U;
    testFailWaitCreate(A_TRUE);
    assert(aPollInitStatic(&config, &instance) == A_STATUS_NO_MEMORY);
    testFailWaitCreate(A_FALSE);
#endif
    assert(testAllocations() == baseline);
}

int main(void)
{
    periodic();
    notifications();
    fairness();
    fifo();
    failures();
    assert(testAllocations() == 0U);
    puts("aPoll timing/wrap/events/concurrency/fairness/lifetime passed");
    return 0;
}
