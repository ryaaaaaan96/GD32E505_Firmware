#include "aComm_table_internal.h"
#include <string.h>

void aCommPlanConfigStructInit(aCommPlanConfig_t *config)
{
    if (config != NULL) *config = (aCommPlanConfig_t){0};
}
void aCommPlanRequestStructInit(aCommPlanRequest_t *request)
{
    if (request != NULL) *request = (aCommPlanRequest_t){0};
}

static aStatus_t table_check(const aCommPlanConfig_t *config)
{
    if (config == NULL || config->items == NULL || config->count == 0U ||
        config->ops == NULL || config->ops->get == NULL ||
        config->ops->execute == NULL ||
        ((config->ops->acquire == NULL) != (config->ops->release == NULL))) {
        return A_STATUS_INVALID_PARAM;
    }
    for (size_t i = 0U; i < config->count; i++) {
        const aCommEntry_t *entry = config->ops->get(config->items, i);
        if (entry == NULL ||
            (entry->poll_time_ms > INT32_MAX &&
             entry->poll_time_ms != ACOMM_POLL_ONCE) ||
            (entry->flags & ~ACOMM_MAP_FLAG_MASK) != 0U) {
            return A_STATUS_INVALID_PARAM;
        }
        if (config->ops->check != NULL) {
            aStatus_t status = config->ops->check(config->context, entry);
            if (status != A_STATUS_OK) return status;
        }
    }
    return A_STATUS_OK;
}

static aStatus_t frame_store(const aCommPlanConfig_t *config,
    aCommPlan_t *plan, size_t index, aCommPlanFrame_t *frame)
{
    if (plan == NULL) return A_STATUS_OK;
    if (index >= plan->capacity) return A_STATUS_INVALID_PARAM;
    const aCommBatch_t batch = {
        config->items, plan->indices + frame->first, frame->count
    };
    if (config->ops->prepare != NULL) {
        aStatus_t status = config->ops->prepare(config->context,
            &batch, &frame->request);
        if (status != A_STATUS_OK) return status;
        if (frame->request.size > sizeof(frame->request.data)) {
            return A_STATUS_INVALID_PARAM;
        }
    }
    plan->frames[index] = *frame;
    return A_STATUS_OK;
}

/* 周期稳定分组；同组保持原表顺序，合并规则完全交给注册回调。
 * 两遍共用规则：统计时不生成报文，填充时才调用 prepare。
 */
static aStatus_t plan_build(const aCommPlanConfig_t *config,
    aCommPlan_t *plan, size_t *frames, size_t *items, size_t *groups)
{
    size_t count = 0U, used = 0U, group_count = 0U;
    for (size_t group = 0U; group < config->count; group++) {
        uint32_t period = config->ops->get(config->items, group)->poll_time_ms;
        aBool_t seen = A_FALSE;
        if (period == 0U) continue;
        for (size_t j = 0U; j < group; j++) {
            if (config->ops->get(config->items, j)->poll_time_ms == period) {
                seen = A_TRUE;
                break;
            }
        }
        if (seen) continue;
        group_count++;
        aCommPlanFrame_t frame = {0};
        const aCommEntry_t *first = NULL, *previous = NULL;
        for (size_t i = group; i < config->count; i++) {
            const aCommEntry_t *entry = config->ops->get(config->items, i);
            if (entry->poll_time_ms != period) continue;
            if (first == NULL ||
                (entry->flags & ACOMM_MAP_FLAG_BREAK_BEFORE) != 0U ||
                config->ops->merge == NULL ||
                !config->ops->merge(config->context, first, previous, entry)) {
                if (first != NULL) {
                    aStatus_t status = frame_store(config, plan, count,
                        &frame);
                    if (status != A_STATUS_OK) return status;
                    count++;
                }
                frame = (aCommPlanFrame_t){
                    .first = used, .poll_time_ms = period
                };
                first = entry;
            }
            if (plan != NULL) {
                if (used >= plan->index_capacity) return A_STATUS_INVALID_PARAM;
                plan->indices[used] = i;
            }
            used++;
            frame.count++;
            previous = entry;
        }
        if (first != NULL) {
            aStatus_t status = frame_store(config, plan, count, &frame);
            if (status != A_STATUS_OK) return status;
            count++;
        }
    }
    *frames = count;
    *items = used;
    *groups = group_count;
    return A_STATUS_OK;
}

aStatus_t aCommPlanMeasure(const aCommPlanConfig_t *config,
    size_t *frames, size_t *items, size_t *groups)
{
    aStatus_t status = table_check(config);
    return status == A_STATUS_OK ?
        plan_build(config, NULL, frames, items, groups) : status;
}

aStatus_t aCommPlanBind(const aCommPlanConfig_t *config, aCommPlan_t *plan)
{
    size_t frames, items, groups;
    if (plan == NULL) return A_STATUS_INVALID_PARAM;
    if (plan->ready) return A_STATUS_BUSY;
    aStatus_t status = aCommPlanMeasure(config, &frames, &items, &groups);
    if (status != A_STATUS_OK) return status;
    if (plan->capacity < frames || plan->index_capacity < items ||
        (frames != 0U && plan->frames == NULL) ||
        (items != 0U && plan->indices == NULL)) return A_STATUS_INVALID_PARAM;
    status = plan_build(config, plan, &plan->count, &items, &groups);
    if (status != A_STATUS_OK) return status;
    /* 全部准备成功后才建立借用，避免失败回滚再次调用释放回调。 */
    if (config->ops->acquire != NULL) {
        status = config->ops->acquire(config->context);
        if (status != A_STATUS_OK) return status;
    }
    plan->config = *config;
    plan->dynamic = A_FALSE;
    plan->ready = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aCommPlanUnbind(aCommPlan_t *plan)
{
    if (!plan->ready) return A_STATUS_NOT_READY;
    if (plan->config.ops->release != NULL) {
        aStatus_t status = plan->config.ops->release(plan->config.context);
        if (status != A_STATUS_OK) return status;
    }
    plan->ready = A_FALSE;
    return A_STATUS_OK;
}
#if ACOMM_STATIC_ENABLE
aStatus_t aCommPlanInitStatic(const aCommPlanConfig_t *config,
    aCommPlan_t *plan)
{
    return aCommPlanBind(config, plan);
}
aStatus_t aCommPlanDeInitStatic(aCommPlan_t *plan)
{
    if (plan == NULL || plan->dynamic) return A_STATUS_INVALID_PARAM;
    return aCommPlanUnbind(plan);
}
#endif
#if ACOMM_DYNAMIC_ENABLE
aStatus_t aCommPlanCreate(const aCommPlanConfig_t *config,
    aCommPlan_t **plan_out)
{
    size_t frames, items, groups;
    if (plan_out == NULL) return A_STATUS_INVALID_PARAM;
    *plan_out = NULL;
    aStatus_t status = aCommPlanMeasure(config, &frames, &items, &groups);
    if (status != A_STATUS_OK) return status;
    if (frames > (SIZE_MAX - sizeof(aCommPlan_t)) / sizeof(aCommPlanFrame_t)) {
        return A_STATUS_INVALID_PARAM;
    }
    size_t size = sizeof(aCommPlan_t) + frames * sizeof(aCommPlanFrame_t);
    if (items > (SIZE_MAX - size) / sizeof(size_t)) {
        return A_STATUS_INVALID_PARAM;
    }
    aCommPlan_t *plan = aOSAlloc(size + items * sizeof(size_t));
    if (plan == NULL) return A_STATUS_NO_MEMORY;
    *plan = (aCommPlan_t){.frames = (void *)(plan + 1), .capacity = frames,
                         .index_capacity = items};
    plan->indices = (void *)(plan->frames + frames);
    status = aCommPlanBind(config, plan);
    if (status != A_STATUS_OK) { aOSFree(plan); return status; }
    plan->dynamic = A_TRUE;
    *plan_out = plan;
    return A_STATUS_OK;
}
aStatus_t aCommPlanDestroy(aCommPlan_t *plan)
{
    if (plan == NULL) return A_STATUS_OK;
    if (!plan->dynamic) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aCommPlanUnbind(plan);
    if (status == A_STATUS_OK) aOSFree(plan);
    return status;
}
#endif
size_t aCommPlanFrameCount(const aCommPlan_t *plan)
{
    return plan != NULL && plan->ready ? plan->count : 0U;
}
size_t aCommPlanGroupCount(const aCommPlan_t *plan)
{
    size_t count = 0U;
    if (plan == NULL || !plan->ready) return 0U;
    for (size_t i = 0U; i < plan->count; i++) {
        if (i == 0U || plan->frames[i].poll_time_ms !=
                       plan->frames[i - 1U].poll_time_ms) count++;
    }
    return count;
}
aStatus_t aCommPlanGroupGet(const aCommPlan_t *plan, size_t index,
    aCommPlanGroup_t *group)
{
    if (plan == NULL || group == NULL) return A_STATUS_INVALID_PARAM;
    if (!plan->ready) return A_STATUS_NOT_READY;
    for (size_t i = 0U; i < plan->count;) {
        size_t first = i++;
        uint32_t period = plan->frames[first].poll_time_ms;
        while (i < plan->count && plan->frames[i].poll_time_ms == period) i++;
        if (index-- == 0U) {
            *group = (aCommPlanGroup_t){first, i - first, period};
            return A_STATUS_OK;
        }
    }
    return A_STATUS_NOT_FOUND;
}
aStatus_t aCommPlanExecute(aCommPlan_t *plan,
    const aCommPlanRequest_t *request)
{
    aCommStepResult_t execution = {
        .status = A_STATUS_INVALID_PARAM, .advance = A_TRUE
    };
    aCommEndpointResult_t response = {0};
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    if (plan != NULL) {
        if (!plan->ready) execution.status = A_STATUS_NOT_READY;
        else if (request->frame_index < plan->count) {
            const aCommPlanFrame_t *frame = &plan->frames[request->frame_index];
            const aCommBatch_t batch = {
                plan->config.items, plan->indices + frame->first, frame->count
            };
            plan->config.ops->execute(plan->config.context, &batch,
                &frame->request, &execution, &response);
        }
    }
    if (request->execution != NULL) *request->execution = execution;
    if (request->result != NULL) *request->result = response;
    return execution.status;
}
