#include "aComm_mapping_internal.h"
#include <string.h>
void aCommPlanConfigStructInit(aCommPlanConfig_t *config)
{
    if (config != NULL) {
        const aCommPlanConfig_t defaults = {
            .unit_id = 1U, .timeout = A_TIMEOUT_MS(1000U)
        };
        *config = defaults;
    }
}

void aCommPlanRequestStructInit(aCommPlanRequest_t *request)
{
    if (request != NULL) {
        const aCommPlanRequest_t defaults = {0};
        *request = defaults;
    }
}

static aStatus_t plan_check(aCommMapping_t *handle,
    const aCommPlanConfig_t *config)
{
    if (handle == NULL || config == NULL || config->items == NULL ||
        config->count == 0U || !aTimeoutIsValid(config->timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (handle->role != ACOMM_MAPPING_CLIENT) return A_STATUS_UNSUPPORTED;
    for (size_t i = 0U; i < config->count; i++) {
        aStatus_t status = aCommMappingClientMapCheck(handle->config,
            &config->items[i], config->unit_id);
        if (status != A_STATUS_OK) return status;
    }
    if (handle->plan_count == SIZE_MAX) return A_STATUS_BUSY;
    return A_STATUS_OK;
}
/* 仅在实际存储时请求端点预编码；容量统计不生成报文。 */
static aStatus_t frame_store(aCommEndpoint_t *endpoint,
    aCommPlan_t *plan, size_t index, aCommPlanFrame_t *frame,
    const aCommMappingClientMap_t *first, uint8_t unit_id)
{
    if (plan == NULL) return A_STATUS_OK;
    const aCommAddress_t address = {
        unit_id, first->area, first->address, frame->quantity
    };
    frame->request = (aCommPrepared_t){.address = address};
    if (endpoint->ops->address->prepare != NULL) {
        aStatus_t status = endpoint->ops->address->prepare(endpoint,
            &address, &frame->request);
        if (status != A_STATUS_OK) return status;
        if (frame->request.size > sizeof(frame->request.data)) {
            return A_STATUS_INVALID_PARAM;
        }
    }
    plan->frames[index] = *frame;
    return A_STATUS_OK;
}
/* 先统计容量，再填充；两遍共用分组规则，原始清单已由 plan_check 校验。 */
static aStatus_t plan_build(aCommEndpoint_t *endpoint,
    const aCommPlanConfig_t *config,
    aCommPlan_t *plan, size_t *frame_count, size_t *item_count)
{
    size_t count = 0U, used = 0U;
    for (size_t group = 0U; group < config->count; group++) {
        uint32_t period = config->items[group].poll_time_ms;
        aBool_t seen = A_FALSE;
        if (period == 0U) continue;
        for (size_t j = 0U; j < group; j++) {
            if (config->items[j].poll_time_ms == period) {
                seen = A_TRUE;
                break;
            }
        }
        if (seen) continue;
        aCommPlanFrame_t frame = {0};
        const aCommMappingClientMap_t *first = NULL;
        for (size_t i = group; i < config->count; i++) {
            const aCommMappingClientMap_t *item = &config->items[i];
            if (item->poll_time_ms != period) continue;
            const aCommAddress_t accumulated = {
                config->unit_id, first == NULL ? 0U : first->area,
                first == NULL ? 0U : first->address, frame.quantity
            };
            const aCommAddress_t next = {config->unit_id, item->area,
                item->address, item->quantity};
            aCommAreaInfo_t area = aCommMappingArea(endpoint, item->area);
            size_t capacity = area.unit_bits == 1U ?
                ACOMM_VALUE_CAPACITY * 8U :
                ACOMM_VALUE_CAPACITY / (area.unit_bits / 8U);
            if (first == NULL ||
                (size_t)frame.quantity + item->quantity > capacity ||
                (item->flags & ACOMM_MAP_FLAG_BREAK_BEFORE) != 0U ||
                endpoint->ops->address->merge == NULL ||
                !endpoint->ops->address->merge(endpoint, &accumulated, &next)) {
                if (first != NULL) {
                    aStatus_t status = frame_store(endpoint, plan, count,
                        &frame, first, config->unit_id);
                    if (status != A_STATUS_OK) return status;
                    count++;
                }
                frame = (aCommPlanFrame_t){
                    .first = used, .poll_time_ms = period
                };
                first = item;
            }
            if (plan != NULL) plan->indices[used] = i;
            used++;
            frame.count++;
            frame.quantity = (uint16_t)(frame.quantity + item->quantity);
        }
        if (first != NULL) {
            aStatus_t status = frame_store(endpoint, plan, count, &frame,
                first, config->unit_id);
            if (status != A_STATUS_OK) return status;
            count++;
        }
    }
    *frame_count = count;
    *item_count = used;
    return A_STATUS_OK;
}

static aStatus_t plan_bind(aCommMapping_t *handle,
    const aCommPlanConfig_t *config, aCommPlan_t *plan,
    aBool_t dynamic)
{
    size_t item_count;
    aStatus_t status = plan_build(handle->config->protocol,
        config, plan, &plan->count, &item_count);
    if (status != A_STATUS_OK) return status;
    plan->config = *config;
    plan->owner = handle;
    plan->dynamic = dynamic;
    plan->ready = A_TRUE;
    handle->plan_count++;
    return A_STATUS_OK;
}

static aStatus_t plan_deinit(aCommPlan_t *plan)
{
    if (!plan->ready) return A_STATUS_NOT_READY;
    if (aAtomicLoad(&plan->owner->active)) {
        return A_STATUS_BUSY;
    }
    plan->owner->plan_count--;
    plan->owner = NULL;
    plan->ready = A_FALSE;
    return A_STATUS_OK;
}
#if ACOMM_STATIC_ENABLE
aStatus_t aCommPlanInitStatic(aCommMapping_t *handle,
    const aCommPlanConfig_t *config, aCommPlan_t *plan)
{
    size_t count, item_count;
    if (plan == NULL) return A_STATUS_INVALID_PARAM;
    if (plan->ready) return A_STATUS_BUSY;
    aStatus_t status = plan_check(handle, config);
    if (status != A_STATUS_OK) return status;
    status = plan_build(handle->config->protocol, config, NULL,
        &count, &item_count);
    if (status != A_STATUS_OK) return status;
    if ((count != 0U && plan->frames == NULL) ||
        (item_count != 0U && plan->indices == NULL) ||
        plan->capacity < count || plan->index_capacity < item_count) {
        return A_STATUS_INVALID_PARAM;
    }
    return plan_bind(handle, config, plan, A_FALSE);
}

aStatus_t aCommPlanDeInitStatic(aCommPlan_t *plan)
{
    if (plan == NULL || plan->dynamic) return A_STATUS_INVALID_PARAM;
    return plan_deinit(plan);
}
#endif
#if ACOMM_DYNAMIC_ENABLE
aStatus_t aCommPlanCreate(aCommMapping_t *handle,
    const aCommPlanConfig_t *config, aCommPlan_t **plan_out)
{
    aCommPlan_t *plan;
    size_t count, item_count;
    aStatus_t status;
    if (plan_out == NULL) return A_STATUS_INVALID_PARAM;
    *plan_out = NULL;
    status = plan_check(handle, config);
    if (status != A_STATUS_OK) return status;
    status = plan_build(handle->config->protocol, config, NULL,
        &count, &item_count);
    if (status != A_STATUS_OK) return status;
    if (count > (SIZE_MAX - sizeof(*plan)) / sizeof(*plan->frames)) {
        return A_STATUS_INVALID_PARAM;
    }
    size_t bytes = sizeof(*plan) + count * sizeof(*plan->frames);
    if (item_count > (SIZE_MAX - bytes) / sizeof(*plan->indices)) {
        return A_STATUS_INVALID_PARAM;
    }
    bytes += item_count * sizeof(*plan->indices);
    plan = aOSAlloc(bytes);
    if (plan == NULL) return A_STATUS_NO_MEMORY;
    memset(plan, 0, sizeof(*plan));
    plan->frames = (void *)(plan + 1);
    plan->indices = (void *)(plan->frames + count);
    plan->capacity = count;
    plan->index_capacity = item_count;
    status = plan_bind(handle, config, plan, A_TRUE);
    if (status != A_STATUS_OK) { aOSFree(plan); return status; }
    *plan_out = plan;
    return A_STATUS_OK;
}

aStatus_t aCommPlanDestroy(aCommPlan_t *plan)
{
    aStatus_t status;
    if (plan == NULL) return A_STATUS_OK;
    if (!plan->dynamic) return A_STATUS_INVALID_PARAM;
    status = plan_deinit(plan);
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

aStatus_t aCommPlanGroupGet(
    const aCommPlan_t *plan, size_t index,
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
    aCommMappingDataBuffer_t buffer;
    aCommEndpointResult_t response = {0};
    aCommStepResult_t execution = {.advance = A_TRUE};
    aStatus_t status = A_STATUS_INVALID_PARAM;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    if (plan == NULL) goto done;
    if (!plan->ready) {
        status = A_STATUS_NOT_READY;
        goto done;
    }
    if (request->frame_index >= plan->count) goto done;
    aCommMapping_t *handle = plan->owner;
    status = aCommMappingOperationBegin(handle, ACOMM_MAPPING_CLIENT,
        plan->config.timeout);
    if (status == A_STATUS_OK) {
        const aCommPlanFrame_t *frame =
            &plan->frames[request->frame_index];
        const aCommReadAccess_t access = {
            .target = &frame->request.address,
            .data = &buffer, .size = sizeof(buffer),
            .timeout = aCommMappingRemaining(handle), .result = &response
        };
        aCommEndpoint_t *endpoint = handle->config->protocol;
        if (frame->request.size != 0U &&
            endpoint->ops->address->read_prepared != NULL) {
            status = endpoint->ops->address->read_prepared(endpoint,
                &frame->request, &access);
        } else {
            status = aCommEndpointRead(endpoint, &access);
        }
        execution.retryable = response.retryable;
        execution.advance = response.started || status != A_STATUS_BUSY;
        if (status == A_STATUS_OK) {
            status = aCommMappingDataPublish(handle, plan->config.items,
                plan->indices + frame->first, frame->count, &buffer);
        }
        aAtomicStore(&handle->active, A_FALSE);
    } else if (status == A_STATUS_BUSY) {
        execution.advance = A_FALSE;
    }
done:
    execution.status = status;
    if (request->execution != NULL) *request->execution = execution;
    if (request->result != NULL) *request->result = response;
    return status;
}
