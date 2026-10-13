#include "aComm_instance.h"
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>

void testSetTime(uint32_t ms);
void testAdvanceTime(uint32_t ms);
unsigned testAllocations(void);
void testFailAllocation(aBool_t fail);
void testFailWaitCreate(aBool_t fail);
void testFailMutexUnlock(aBool_t fail);
void aOSDelayMs(uint32_t ms) { testAdvanceTime(ms); }

static aBool_t process_on_notify;

static aCommHandle_t *poll;
static unsigned executed, completed, events;
static aBool_t advance = A_TRUE, repost, retryable;
static aStatus_t execution_status = A_STATUS_OK;
static size_t step_seen;
#if ACOMM_STATIC_ENABLE
static aCommJobState_t jobs[3];
static aCommEventState_t event_states[40];
static aCommRequest_t requests[2];
static aCommHandle_t instance = {
    .jobs = jobs, .job_capacity = 3U,
    .events = event_states, .event_capacity = 40U,
    .requests = requests, .request_capacity = 2U
};
#endif

static void execute(void *context, size_t step, aCommStepResult_t *result)
{
    aCommProcessResult_t nested;
    (void)context;
    executed++;
    step_seen = step;
    assert(aCommProcess(poll, &nested) == A_STATUS_BUSY);
    assert(aCommWait(poll, A_TIMEOUT_NO_WAIT) == A_STATUS_BUSY);
    result->status = execution_status;
    result->advance = advance;
    result->retryable = retryable;
}

static aStatus_t request_execute(void *context)
{
    aCommStepResult_t result = {0};
    execute(context, 0U, &result);
    return result.status;
}

static void event(void *context)
{
    (void)context;
    events++;
    if (repost) {
        repost = A_FALSE;
        assert(aCommEventPost(poll, 0U) == A_STATUS_OK);
    }
}

static void complete(void *context, aStatus_t status)
{
    (void)context;
    assert(status == execution_status);
    completed++;
}

static void create(const aCommConfig_t *config)
{
#if ACOMM_STATIC_ENABLE
    assert(aCommInitStatic(config, &instance) == A_STATUS_OK);
    poll = &instance;
    assert(aCommInitStatic(config, &instance) == A_STATUS_BUSY);
#else
    assert(aCommCreate(config, &poll) == A_STATUS_OK);
#endif
}

static aStatus_t destroy(void)
{
#if ACOMM_STATIC_ENABLE
    return aCommDeInitStatic(poll);
#else
    return aCommDestroy(poll);
#endif
}

static aCommProcessResult_t process(void)
{
    aCommProcessResult_t result;
    assert(aCommProcess(poll, &result) == A_STATUS_OK);
    return result;
}

void __real_aOSWaitObjectNotify(aOSWaitObject_t object);
void __wrap_aOSWaitObjectNotify(aOSWaitObject_t object)
{
    __real_aOSWaitObjectNotify(object);
    /* 模拟唤醒高优先级消费者，在 Submit 返回前完成请求。 */
    if (process_on_notify) {
        process_on_notify = A_FALSE;
        assert(process().type == ACOMM_WORK_REQUEST);
    }
}

static void periodic(void)
{
    aCommJobConfig_t job;
    aCommConfig_t config;
    aCommProcessResult_t result;
    aCommJobConfigStructInit(&job);
    aCommConfigStructInit(&config);
    job.execute = execute;
    job.step_count = 2U;
    job.poll_time_ms = 20U;
    job.interval_ms = 1U;
    config.jobs = &job;
    config.job_count = 1U;
    testSetTime(UINT32_MAX - 8U);
    create(&config);
    result = process();
    assert(result.type == ACOMM_WORK_JOB && step_seen == 0U);
    assert(result.wait.milliseconds == 1U);
    assert(aCommWait(poll, result.wait) == A_STATUS_TIMEOUT);
    result = process();
    assert(result.type == ACOMM_WORK_JOB && step_seen == 1U);
    assert(result.wait.milliseconds == 19U);
    assert(aCommWait(poll, result.wait) == A_STATUS_TIMEOUT);
    result = process();
    assert(step_seen == 0U && result.type == ACOMM_WORK_JOB);
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
    assert(aCommJobTrigger(poll, 0U) == A_STATUS_OK);
    assert(aCommWait(poll, result.wait) == A_STATUS_OK);
    result = process();
    assert(result.type == ACOMM_WORK_NONE && result.wait.milliseconds == 1U);
    testAdvanceTime(1U);
    result = process();
    assert(result.type == ACOMM_WORK_JOB && step_seen == 0U);
    assert(destroy() == A_STATUS_OK);
}

static void *poster(void *context)
{
    size_t id = *(const size_t *)context;
    for (unsigned i = 0U; i < 10000U; i++) {
        assert(aCommEventPost(poll, id) == A_STATUS_OK);
    }
    return NULL;
}

static void notifications(void)
{
    aCommConfig_t config;
    aCommEventConfig_t table[40];
    aCommProcessResult_t result;
    pthread_t threads[2];
    const size_t ids[] = {0U, 39U};
    aCommConfigStructInit(&config);
    for (size_t i = 0U; i < 40U; i++) {
        aCommEventConfigStructInit(&table[i]);
        table[i].function = event;
    }
    config.events = table;
    config.event_count = 40U;
    create(&config);
    result = process();
    assert(result.type == ACOMM_WORK_NONE);
    assert(result.wait.type == A_TIMEOUT_TYPE_FOREVER);
    /* 事件到达位于 Process 与 Wait 之间，通知必须留存。 */
    assert(aCommEventPost(poll, 0U) == A_STATUS_OK);
    assert(aCommEventPost(poll, 0U) == A_STATUS_OK);
    assert(aCommWait(poll, result.wait) == A_STATUS_OK);
    events = 0U;
    repost = A_TRUE;
    result = process();
    assert(events == 1U && result.wait.milliseconds == 0U);
    result = process();
    assert(events == 2U && result.type == ACOMM_WORK_EVENT);
    result = process();
    assert(result.type == ACOMM_WORK_NONE);
    for (size_t i = 0U; i < 2U; i++) {
        assert(pthread_create(&threads[i], NULL, poster, (void *)&ids[i]) == 0);
    }
    for (size_t i = 0U; i < 2U; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
    assert(process().type == ACOMM_WORK_EVENT);
    assert(process().type == ACOMM_WORK_EVENT);
    assert(process().type == ACOMM_WORK_NONE);
    assert(events == 4U);
    assert(aCommEventPost(poll, 40U) == A_STATUS_NOT_FOUND);
    assert(destroy() == A_STATUS_OK);
}

static void fairness(void)
{
    aCommConfig_t config;
    aCommJobConfig_t job;
    aCommEventConfig_t ev;
    aCommRequest_t request;
    aCommProcessResult_t result;
    unsigned allocations;
    aCommConfigStructInit(&config);
    aCommJobConfigStructInit(&job);
    aCommEventConfigStructInit(&ev);
    aCommRequestStructInit(&request);
    job.execute = execute;
    ev.function = event;
    request.execute = request_execute;
    request.complete = complete;
    config.jobs = &job;
    config.job_count = 1U;
    config.events = &ev;
    config.event_count = 1U;
    config.request_capacity = 2U;
    create(&config);
    allocations = testAllocations();
    assert(aCommSubmit(poll, &request) == A_STATUS_OK);
    assert(aCommSubmit(poll, &request) == A_STATUS_OK);
    assert(aCommSubmit(poll, &request) == A_STATUS_BUSY);
    assert(destroy() == A_STATUS_BUSY);
    executed = events = completed = 0U;
    for (unsigned i = 0U; i < 30U; i++) {
        assert(aCommEventPost(poll, 0U) == A_STATUS_OK);
        result = process();
        assert(result.type == (aCommWorkType_t)(1U + i % 3U));
        if (result.type == ACOMM_WORK_REQUEST) {
            assert(aCommSubmit(poll, &request) == A_STATUS_OK);
        }
    }
    assert(events == 10U && completed == 10U && executed == 20U);
    assert(testAllocations() == allocations);
    while (completed < 12U) (void)process();
    assert(aOSMutexLock(poll->queue_mutex, A_TIMEOUT_NO_WAIT) == A_STATUS_OK);
    poll->class_cursor = 2U;
    result = process();
    assert(result.type == ACOMM_WORK_JOB && result.wait.milliseconds == 0U);
    assert(aOSMutexUnlock(poll->queue_mutex) == A_STATUS_OK);
    assert(destroy() == A_STATUS_OK);
}

static unsigned fifo_count;
static unsigned fifo_values[] = {0U, 1U, 2U};

static aStatus_t fifo_execute(void *context)
{
    aCommRequest_t followup;
    unsigned value = *(const unsigned *)context;
    assert(value == fifo_count++);
    if (value == 0U) {
        /* 消费已释放队列锁，回调可以利用空槽再提交一条。 */
        aCommRequestStructInit(&followup);
        followup.execute = fifo_execute;
        followup.context = &fifo_values[2];
        assert(aCommSubmit(poll, &followup) == A_STATUS_OK);
    }
    return A_STATUS_ERROR;
}

static void fifo(void)
{
    aCommConfig_t config;
    aCommRequest_t request;
    aCommConfigStructInit(&config);
    aCommRequestStructInit(&request);
    config.request_capacity = 2U;
    create(&config);
    request.execute = fifo_execute;
    request.context = &fifo_values[0];
    assert(aCommSubmit(poll, &request) == A_STATUS_OK);
    request.context = &fifo_values[1];
    assert(aCommSubmit(poll, &request) == A_STATUS_OK);
    /* 入队复制描述；调用者改写局部描述不改变已排队请求。 */
    request.execute = NULL;
    for (unsigned i = 0U; i < 3U; i++) {
        aCommProcessResult_t result = process();
        assert(result.type == ACOMM_WORK_REQUEST);
        assert(result.execution.status == A_STATUS_ERROR);
    }
    assert(fifo_count == 3U && process().type == ACOMM_WORK_NONE);
    assert(destroy() == A_STATUS_OK);
}

static void failures(void)
{
    aCommConfig_t config;
    unsigned baseline = testAllocations();
    aCommConfigStructInit(&config);
#if ACOMM_DYNAMIC_ENABLE
    aCommHandle_t *allocated;
    testFailAllocation(A_TRUE);
    assert(aCommCreate(&config, &allocated) == A_STATUS_NO_MEMORY);
    assert(allocated == NULL);
    testFailAllocation(A_FALSE);
    testFailWaitCreate(A_TRUE);
    assert(aCommCreate(&config, &allocated) == A_STATUS_NO_MEMORY);
    assert(testAllocations() == baseline);
    testFailWaitCreate(A_FALSE);
    assert(aCommCreate(&config, &allocated) == A_STATUS_OK);
    assert(aCommDestroy(allocated) == A_STATUS_OK);
    config.request_capacity = SIZE_MAX;
    assert(aCommCreate(&config, &allocated) == A_STATUS_INVALID_PARAM);
#endif
#if ACOMM_STATIC_ENABLE
    aCommConfigStructInit(&config);
    config.request_capacity = 3U;
    assert(aCommInitStatic(&config, &instance) == A_STATUS_INVALID_PARAM);
    config.request_capacity = 0U;
    testFailWaitCreate(A_TRUE);
    assert(aCommInitStatic(&config, &instance) == A_STATUS_NO_MEMORY);
    testFailWaitCreate(A_FALSE);
#endif
    assert(testAllocations() == baseline);
}

static void period_limits(void)
{
    aCommJobConfig_t job;
    aCommConfig_t config;
    const uint32_t invalid[] = {INT32_MAX + 1U, UINT32_MAX - 1U};
    aCommJobConfigStructInit(&job);
    aCommConfigStructInit(&config);
    job.execute = execute;
    config.jobs = &job;
    config.job_count = 1U;
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        job.poll_time_ms = invalid[i];
#if ACOMM_STATIC_ENABLE
        assert(aCommInitStatic(&config, &instance) == A_STATUS_INVALID_PARAM);
#endif
#if ACOMM_DYNAMIC_ENABLE
        aCommHandle_t *allocated;
        assert(aCommCreate(&config, &allocated) == A_STATUS_INVALID_PARAM);
        assert(allocated == NULL);
#endif
    }
    /* 最大普通周期必须保持周期执行，不能误认为单轮标记。 */
    job.poll_time_ms = INT32_MAX;
    testSetTime(UINT32_MAX - 8U);
    create(&config);
    aCommProcessResult_t result = process();
    assert(result.type == ACOMM_WORK_JOB);
    assert(result.wait.type == A_TIMEOUT_TYPE_RELATIVE);
    assert(result.wait.milliseconds == INT32_MAX);
    testAdvanceTime(INT32_MAX - 1U);
    assert(process().type == ACOMM_WORK_NONE);
    testAdvanceTime(1U);
    assert(process().type == ACOMM_WORK_JOB);
    assert(destroy() == A_STATUS_OK);
}

static void one_shot(void)
{
    aCommJobConfig_t job;
    aCommConfig_t config;
    aCommProcessResult_t result;
    aCommJobConfigStructInit(&job);
    aCommConfigStructInit(&config);
    job.execute = execute;
    job.step_count = 2U;
    job.poll_time_ms = ACOMM_POLL_ONCE;
    job.interval_ms = 3U;
    config.jobs = &job;
    config.job_count = 1U;
    config.request_capacity = 1U;
    advance = A_FALSE;
    execution_status = A_STATUS_BUSY;
    testSetTime(UINT32_MAX - 2U);
    create(&config);
    result = process();
    assert(result.type == ACOMM_WORK_JOB && step_seen == 0U);
    assert(result.wait.milliseconds == job.error_delay_ms);
    assert(process().type == ACOMM_WORK_NONE);
    /* 跨时基回绕重试；单轮标记不能影响步骤间隔或重试等待。 */
    testAdvanceTime(job.error_delay_ms);
    advance = A_TRUE;
    execution_status = A_STATUS_OK;
    result = process();
    assert(result.type == ACOMM_WORK_JOB && step_seen == 0U);
    assert(result.wait.milliseconds == job.interval_ms);
    assert(aCommJobTrigger(poll, 0U) == A_STATUS_OK);
    assert(process().type == ACOMM_WORK_NONE);
    testAdvanceTime(job.interval_ms);
    result = process();
    assert(result.type == ACOMM_WORK_JOB && step_seen == 1U);
    assert(result.wait.type == A_TIMEOUT_TYPE_FOREVER);
    assert(process().type == ACOMM_WORK_NONE);
    assert(aCommJobTrigger(poll, 0U) == A_STATUS_OK);
    testAdvanceTime(UINT32_MAX);
    result = process();
    assert(result.type == ACOMM_WORK_NONE);
    assert(result.wait.type == A_TIMEOUT_TYPE_FOREVER);
    testAdvanceTime(1U);
    result = process();
    assert(result.type == ACOMM_WORK_NONE);
    assert(result.wait.type == A_TIMEOUT_TYPE_FOREVER);
    assert(destroy() == A_STATUS_OK);
}

static void submissions(void)
{
    aCommConfig_t config;
    aCommRequest_t request = {.execute = request_execute, .complete = complete};
    aCommConfigStructInit(&config);
    config.request_capacity = 1U;
    create(&config);
    executed = completed = 0U;
    assert(aCommSubmit(NULL, &request) == A_STATUS_INVALID_PARAM);
    assert(aCommSubmit(poll, NULL) == A_STATUS_INVALID_PARAM);
    assert(aCommSubmit(poll, &request) == A_STATUS_OK);
    assert(aCommSubmit(poll, &request) == A_STATUS_BUSY);
    /* 队列已满，解锁异常仍不代表接收了新请求。 */
    testFailMutexUnlock(A_TRUE);
    g_aOSFaultRecord.code = AOS_FAULT_NONE;
    assert(aCommSubmit(poll, &request) == A_STATUS_BUSY);
    assert(g_aOSFaultRecord.code == AOS_FAULT_MUTEX_UNLOCK);
    testFailMutexUnlock(A_FALSE);
    assert(process().type == ACOMM_WORK_REQUEST);
    assert(executed == 1U && completed == 1U);
    assert(process().type == ACOMM_WORK_NONE);

    /* 已入队的请求保持成功返回，执行失败另由完成回调报告。 */
    testFailMutexUnlock(A_TRUE);
    g_aOSFaultRecord.code = AOS_FAULT_NONE;
    assert(aCommSubmit(poll, &request) == A_STATUS_OK);
    assert(g_aOSFaultRecord.code == AOS_FAULT_MUTEX_UNLOCK);
    assert(g_aOSFaultRecord.status == A_STATUS_ERROR);
    testFailMutexUnlock(A_FALSE);
    execution_status = A_STATUS_ERROR;
    advance = A_FALSE;
    aCommProcessResult_t result = process();
    assert(result.type == ACOMM_WORK_REQUEST);
    assert(result.execution.status == A_STATUS_ERROR);
    assert(executed == 2U && completed == 2U);
    assert(process().type == ACOMM_WORK_NONE);
    execution_status = A_STATUS_OK;
    advance = A_TRUE;

    assert(aOSMutexLock(poll->queue_mutex, A_TIMEOUT_NO_WAIT) == A_STATUS_OK);
    assert(aCommSubmit(poll, &request) == A_STATUS_BUSY);
    assert(aOSMutexUnlock(poll->queue_mutex) == A_STATUS_OK);
    assert(process().type == ACOMM_WORK_NONE);

    process_on_notify = A_TRUE;
    assert(aCommSubmit(poll, &request) == A_STATUS_OK);
    assert(executed == 3U && completed == 3U);
    assert(process().type == ACOMM_WORK_NONE);
    assert(destroy() == A_STATUS_OK);
}

/* 重试策略不依赖任何协议：忙不计数，失败有上限，下一步重新计数。 */
static void retries(void)
{
    aCommJobConfig_t job;
    aCommJobConfigStructInit(&job);
    job.execute = execute;
    job.step_count = 2U;
    job.retry_count = 2U;
    job.poll_time_ms = ACOMM_POLL_ONCE;
    const aCommConfig_t config = {.jobs = &job, .job_count = 1U};
#if ACOMM_STATIC_ENABLE
    jobs[0].retries = UINT16_MAX; /* 静态状态数组也允许带有旧内容。 */
#endif
    create(&config);
    execution_status = A_STATUS_TIMEOUT;
    retryable = A_TRUE;
    advance = A_FALSE;
    for (unsigned i = 0U; i < 4U; i++) {
        assert(!process().execution.advance);
        assert(poll->jobs[0].retries == 0U);
        testAdvanceTime(5U);
    }
    advance = A_TRUE;
    for (size_t step = 0U; step < 2U; step++) {
        for (unsigned retry = 0U; retry < 3U; retry++) {
            aCommProcessResult_t result = process();
            assert(result.step_index == step);
            assert(result.execution.advance == (retry == 2U));
            testAdvanceTime(5U);
        }
    }
    assert(process().type == ACOMM_WORK_NONE);
    assert(destroy() == A_STATUS_OK);
    execution_status = A_STATUS_OK;
    retryable = A_FALSE;
}

int main(void)
{
    period_limits();
    one_shot();
    retries();
    submissions();
    periodic();
    notifications();
    fairness();
    fifo();
    failures();
    assert(testAllocations() == 0U);
    puts("aComm timing/wrap/events/concurrency/fairness/lifetime passed");
    return 0;
}
