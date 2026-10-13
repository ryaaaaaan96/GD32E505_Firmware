#include "aComm_instance.h"
#include <string.h>

void aCommBindingConfigStructInit(aCommBindingConfig_t *config)
{
    if (config != NULL) *config = (aCommBindingConfig_t){0};
}
void aCommValueReadRequestStructInit(aCommValueReadRequest_t *request)
{
    if (request != NULL) *request = (aCommValueReadRequest_t){
        .target.paramIndex = ACOMM_DATA_WHOLE, .timeout = A_TIMEOUT_NO_WAIT
    };
}
void aCommValueWriteRequestStructInit(aCommValueWriteRequest_t *request)
{
    if (request != NULL) *request = (aCommValueWriteRequest_t){
        .target.paramIndex = ACOMM_DATA_WHOLE, .timeout = A_TIMEOUT_NO_WAIT
    };
}
aStatus_t aCommBindingConfigCheck(const aCommBindingConfig_t *config)
{
    if (config == NULL || config->data == NULL || config->data->ops == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    const aCommDataOps_t *ops = config->data->ops->data;
    if (ops == NULL || ops->info == NULL || ops->update == NULL ||
        config->data->ops->read == NULL || config->data->ops->write == NULL) {
        return A_STATUS_UNSUPPORTED;
    }
    if (config->snapshot != NULL && config->snapshot_size == 0U) {
        return A_STATUS_INVALID_PARAM;
    }
    if (ops->subscribe != NULL && (ops->unsubscribe == NULL ||
        ops->complete == NULL || ops->publish == NULL ||
        ops->subscription_size == 0U)) return A_STATUS_INVALID_PARAM;
    return A_STATUS_OK;
}
aStatus_t aCommBindingEntryCheck(const aCommBindingConfig_t *config,
    const aCommBindingEvents_t *events, const aCommEntry_t *entry)
{
    if (entry == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aCommBindingConfigCheck(config);
    if (status != A_STATUS_OK) return status;
    const aCommTargetConfig_t target = {
        entry->deviceID, entry->sigIndex, entry->paramIndex,
        entry->subscribe_events, entry->publish_events
    };
    status = aCommTargetConfigCheck(&target);
    if (status != A_STATUS_OK) return status;
    uint32_t mask = entry->subscribe_events;
    if ((entry->flags & ACOMM_MAP_FLAG_ACCESS_MASK) == 0U ||
        (entry->flags & ~ACOMM_MAP_FLAG_MASK) != 0U ||
        ((mask & ACOMM_REQUEST_READ) != 0U &&
         (entry->flags & ACOMM_MAP_FLAG_READ) == 0U) ||
        ((mask & ACOMM_REQUEST_WRITE) != 0U &&
         (entry->flags & ACOMM_MAP_FLAG_WRITE) == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (mask == 0U || mask == ACOMM_SUBSCRIBE_MARK_ONLY ||
        config->data->ops->data->subscribe == NULL) return A_STATUS_OK;
    if (events == NULL ||
        ((mask & (ACOMM_REQUEST_READ | ACOMM_REQUEST_WRITE)) != 0U &&
         (events->submit == NULL || events->execute == NULL ||
          config->request_capacity == 0U)) ||
        ((mask & (ACOMM_EVENT_UPDATED | ACOMM_EVENT_WRITE_COMPLETE)) != 0U &&
         events->observe == NULL)) return A_STATUS_INVALID_PARAM;
    return A_STATUS_OK;
}

/* 通知只借用数据和登记任务；字段重叠检查在消费者中完成。 */
static aStatus_t requested(void *context)
{
    aCommBindingRequestSlot_t *slot = context;
    aCommBindingSubscription_t *sub = slot->subscription;
    aCommBinding_t *binding = sub->owner;
    const aCommEntry_t *entry = sub->entry;
    const aCommNotifyInput_t *input = &slot->input;
    aCommBindingRequest_t request = {
        .entry = entry,
        .target = {entry->deviceID, entry->sigIndex, entry->paramIndex},
        .write = input->event == ACOMM_REQUEST_WRITE
    };
    if (input->endpoint != binding->config.data ||
        (input->event != ACOMM_REQUEST_READ && !request.write) ||
        input->target.deviceID != entry->deviceID ||
        input->target.sigIndex != entry->sigIndex ||
        (entry->flags & (request.write ? ACOMM_MAP_FLAG_WRITE :
                         ACOMM_MAP_FLAG_READ)) == 0U) {
        return A_STATUS_UNSUPPORTED;
    }
    const aCommDataQuery_t query = {entry->deviceID, entry->sigIndex};
    aStatus_t status = aCommDataInfo(input->endpoint, &query, &request.info);
    if (status != A_STATUS_OK) return status;
    const aCommDataInfo_t *sig = &request.info;
    aCommSpan_t bound = {0U, sig->size};
    if (entry->paramIndex != ACOMM_DATA_WHOLE) {
        if (sig->params == NULL || entry->paramIndex >= sig->param_count) {
            return A_STATUS_NOT_FOUND;
        }
        const aDataField_t *param = &sig->params[entry->paramIndex];
        bound = (aCommSpan_t){param->offset, param->size};
    }
    if (input->overlap.size == 0U || input->overlap.offset < bound.offset ||
        input->overlap.offset - bound.offset > bound.size ||
        input->overlap.size > bound.size -
            (input->overlap.offset - bound.offset)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (input->overlap.offset != bound.offset ||
        input->overlap.size != bound.size) {
        if (sig->type != ALIB_DATA_STRUCT || sig->params == NULL ||
            entry->paramIndex != ACOMM_DATA_WHOLE ||
            input->target.paramIndex >= sig->param_count) {
            return A_STATUS_UNSUPPORTED;
        }
        const aDataField_t *param = &sig->params[input->target.paramIndex];
        if (param->offset != input->overlap.offset ||
            param->size != input->overlap.size) return A_STATUS_UNSUPPORTED;
        request.target.paramIndex = input->target.paramIndex;
    }
    if (request.write) {
        if (input->data == NULL ||
            input->overlap.offset < input->source.offset ||
            input->overlap.offset - input->source.offset > input->source.size ||
            input->overlap.size > input->source.size -
                (input->overlap.offset - input->source.offset)) {
            return A_STATUS_INVALID_PARAM;
        }
        request.source = (const uint8_t *)input->data +
            input->overlap.offset - input->source.offset;
    }
    return binding->events.execute(binding->events.context, &request);
}
static void completed(void *context, aStatus_t status)
{
    aCommBindingRequestSlot_t *slot = context;
    aCommBindingSubscription_t *sub = slot->subscription;
    const aCommNotifyInput_t input = slot->input;
    const aCommDataOps_t *ops = input.endpoint->ops->data;
    if (input.event == ACOMM_REQUEST_WRITE &&
        (sub->entry->publish_events & ACOMM_EVENT_WRITE_COMPLETE) != 0U) {
        const aCommPublishRequest_t publish = {
            .target = input.target,
            .event = ACOMM_EVENT_WRITE_COMPLETE, .status = status
        };
        (void)ops->publish(input.endpoint, &publish);
    }
    /* 完成回调可立即再次提交或销毁宿主；释放槽位后不再访问绑定对象。 */
    aAtomicStore(&slot->busy, A_FALSE);
    aAtomicStore(&sub->busy, A_FALSE);
    (void)ops->complete(input.delivery, status);
}
static aStatus_t notify(void *context, const aCommNotifyInput_t *input,
    aCommNotifyOutput_t *output)
{
    aCommBindingSubscription_t *sub = context;
    aCommBinding_t *binding = sub->owner;
    output->accepted = A_FALSE;
    if (input->event == ACOMM_EVENT_UPDATED ||
        input->event == ACOMM_EVENT_WRITE_COMPLETE) {
        if (binding->events.observe == NULL) return A_STATUS_UNSUPPORTED;
        binding->events.observe(binding->events.context, sub->entry, input);
        output->accepted = A_TRUE;
        return A_STATUS_OK;
    }
    if ((input->event != ACOMM_REQUEST_READ &&
         input->event != ACOMM_REQUEST_WRITE) || input->delivery == NULL ||
        binding->events.execute == NULL || binding->events.submit == NULL) {
        return A_STATUS_UNSUPPORTED;
    }
    aBool_t expected = A_FALSE;
    if (!aAtomicCompareExchangeStrong(&sub->busy, &expected, A_TRUE)) {
        return A_STATUS_BUSY;
    }
    aCommBindingRequestSlot_t *slot = NULL;
    /* 池大小由应用限制；只尝试原子占用，不等待、不申请内存。 */
    for (size_t i = 0U; i < binding->config.request_capacity; i++) {
        expected = A_FALSE;
        if (aAtomicCompareExchangeStrong(&binding->requests[i].busy,
                                        &expected, A_TRUE)) {
            slot = &binding->requests[i];
            break;
        }
    }
    if (slot == NULL) {
        aAtomicStore(&sub->busy, A_FALSE);
        return A_STATUS_BUSY;
    }
    slot->subscription = sub;
    slot->input = *input;
    slot->work = (aCommRequest_t){requested, completed, slot};
    aStatus_t status = binding->events.submit(binding->events.submit_context,
        &slot->work);
    output->accepted = status == A_STATUS_OK;
    /* OK 后可能已完成、重入或销毁宿主，此后禁止访问请求池。 */
    if (!output->accepted) {
        aAtomicStore(&slot->busy, A_FALSE);
        aAtomicStore(&sub->busy, A_FALSE);
    }
    return status;
}

aStatus_t aCommBindingInit(const aCommBindingConfig_t *config,
    const aCommBindingEvents_t *events, aCommBinding_t *binding)
{
    if (binding == NULL) return A_STATUS_INVALID_PARAM;
    if (binding->ready) return A_STATUS_BUSY;
    aStatus_t status = aCommBindingConfigCheck(config);
    if (status != A_STATUS_OK) return status;
    if (binding->subscription_capacity != 0U &&
        binding->subscriptions == NULL) return A_STATUS_INVALID_PARAM;
    if (binding->request_capacity < config->request_capacity ||
        (config->request_capacity != 0U && binding->requests == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    for (size_t i = 0U; i < config->request_capacity; i++) {
        aAtomicInit(&binding->requests[i].busy, A_FALSE);
    }
    binding->config = *config;
    binding->events = events == NULL ? (aCommBindingEvents_t){0} : *events;
    binding->subscription_count = 0U;
    if (binding->config.snapshot == NULL) {
        binding->config.snapshot = binding->snapshot;
        binding->config.snapshot_size = sizeof(binding->snapshot);
    }
    binding->ready = A_TRUE;
    return A_STATUS_OK;
}
aStatus_t aCommBindingSubscribe(aCommBinding_t *binding,
    const aCommEntry_t *entry)
{
    if (binding == NULL) return A_STATUS_INVALID_PARAM;
    if (!binding->ready) return A_STATUS_NOT_READY;
    aStatus_t status = aCommBindingEntryCheck(&binding->config,
        &binding->events, entry);
    if (status != A_STATUS_OK) return status;
    const aCommDataOps_t *ops = binding->config.data->ops->data;
    if (entry->subscribe_events == 0U || ops->subscribe == NULL) {
        return A_STATUS_OK;
    }
    if (binding->subscription_count == binding->subscription_capacity) {
        return A_STATUS_INVALID_PARAM;
    }
    aCommBindingSubscription_t *sub =
        &binding->subscriptions[binding->subscription_count];
    void *native = sub->native;
    if (native == NULL) return A_STATUS_INVALID_PARAM;
    *sub = (aCommBindingSubscription_t){
        .native = native, .entry = entry, .owner = binding
    };
    aAtomicInit(&sub->busy, A_FALSE);
    const aCommSubscriptionConfig_t config = {
        .events = entry->subscribe_events, .type = binding->events.type,
        .owner = binding->events.context, .definition = entry,
        .notify = notify, .context = sub
    };
    const aCommTarget_t target = {
        entry->deviceID, entry->sigIndex, entry->paramIndex
    };
    status = ops->subscribe(binding->config.data, &target, native, &config);
    if (status == A_STATUS_OK) binding->subscription_count++;
    return status;
}
aStatus_t aCommBindingDeInit(aCommBinding_t *binding)
{
    if (binding == NULL) return A_STATUS_INVALID_PARAM;
    if (!binding->ready) return A_STATUS_NOT_READY;
    for (size_t i = 0U; i < binding->subscription_count; i++) {
        if (aAtomicLoad(&binding->subscriptions[i].busy)) return A_STATUS_BUSY;
    }
    while (binding->subscription_count != 0U) {
        aStatus_t status = binding->config.data->ops->data->unsubscribe(
            binding->subscriptions[binding->subscription_count - 1U].native);
        if (status != A_STATUS_OK) return status;
        binding->subscription_count--;
    }
    binding->ready = A_FALSE;
    return A_STATUS_OK;
}

aStatus_t aCommBindingRead(aCommBinding_t *binding,
    const aCommValueReadRequest_t *request)
{
    if (binding == NULL || request == NULL || request->data == NULL ||
        !aTimeoutIsValid(request->timeout)) return A_STATUS_INVALID_PARAM;
    if (!binding->ready) return A_STATUS_NOT_READY;
    if (binding->config.sig_read != NULL) {
        return binding->config.sig_read(binding->config.sig_context, request);
    }
    const aCommDataRead_t read = {
        .deviceID = request->target.deviceID,
        .sigIndex = request->target.sigIndex,
        .paramIndex = request->target.paramIndex,
        .dst = request->data, .size = request->size, .timeout = request->timeout
    };
    return aCommDataRead(binding->config.data, &read);
}
aStatus_t aCommBindingWrite(aCommBinding_t *binding,
    const aCommValueWriteRequest_t *request)
{
    if (binding == NULL || request == NULL || request->data == NULL ||
        !aTimeoutIsValid(request->timeout)) return A_STATUS_INVALID_PARAM;
    if (!binding->ready) return A_STATUS_NOT_READY;
    if (binding->config.sig_write != NULL) {
        return binding->config.sig_write(binding->config.sig_context, request);
    }
    const aCommDataWrite_t write = {
        .deviceID = request->target.deviceID,
        .sigIndex = request->target.sigIndex,
        .paramIndex = request->target.paramIndex,
        .src = request->data, .size = request->size, .timeout = request->timeout
    };
    return aCommDataWrite(binding->config.data, &write);
}
aStatus_t aCommBindingTransfer(aCommBinding_t *binding,
    const aCommBindingTransfer_t *request)
{
    if (binding == NULL || request == NULL || request->edit == NULL ||
        !aTimeoutIsValid(request->timeout)) return A_STATUS_INVALID_PARAM;
    if (!binding->ready) return A_STATUS_NOT_READY;
    if (request->size > binding->config.snapshot_size) {
        return A_STATUS_INVALID_PARAM;
    }
    if (request->write) {
        const aCommDataUpdate_t update = {
            .target = request->target, .buffer = binding->config.snapshot,
            .capacity = binding->config.snapshot_size,
            .edit = request->edit, .context = request->context,
            .timeout = request->timeout
        };
        return aCommDataUpdate(binding->config.data, &update);
    }
    const aCommDataRead_t read = {
        .deviceID = request->target.deviceID,
        .sigIndex = request->target.sigIndex, .paramIndex = ACOMM_DATA_WHOLE,
        .dst = binding->config.snapshot, .size = request->size,
        .timeout = request->timeout
    };
    aStatus_t status = aCommDataRead(binding->config.data, &read);
    return status == A_STATUS_OK ? request->edit(request->context,
        binding->config.snapshot, request->size) : status;
}

/* 一个连续区先放通用槽，再放按标准最大对齐的端点原生节点。 */
static aStatus_t storage_layout(const aCommBindingConfig_t *config,
    size_t count, size_t *nodes, size_t *stride, size_t *size)
{
    aStatus_t status = aCommBindingConfigCheck(config);
    if (status != A_STATUS_OK) return status;
    const size_t align = _Alignof(max_align_t);
    size_t native = config->data->ops->data->subscription_size;
    if (native > SIZE_MAX - (align - 1U) ||
        count > (SIZE_MAX - (align - 1U)) /
            sizeof(aCommBindingSubscription_t)) return A_STATUS_INVALID_PARAM;
    *stride = (native + align - 1U) / align * align;
    size_t requests = (count * sizeof(aCommBindingSubscription_t) +
        align - 1U) / align * align;
    if (config->request_capacity > (SIZE_MAX - requests - (align - 1U)) /
        sizeof(aCommBindingRequestSlot_t)) return A_STATUS_INVALID_PARAM;
    *nodes = requests + config->request_capacity *
        sizeof(aCommBindingRequestSlot_t);
    *nodes = (*nodes + align - 1U) / align * align;
    if (*stride != 0U && count > (SIZE_MAX - *nodes) / *stride) {
        return A_STATUS_INVALID_PARAM;
    }
    *size = *nodes + count * *stride;
    return A_STATUS_OK;
}
aStatus_t aCommBindingStorageSize(const aCommBindingConfig_t *config,
    size_t count, size_t *size)
{
    size_t nodes, stride;
    if (size == NULL) return A_STATUS_INVALID_PARAM;
    return storage_layout(config, count, &nodes, &stride, size);
}
aStatus_t aCommBindingStorageSetup(aCommBinding_t *binding,
    const aCommBindingConfig_t *config, void *storage, size_t count)
{
    size_t nodes, stride, size;
    if (binding == NULL || config == NULL ||
        ((count != 0U || config->request_capacity != 0U) && (storage == NULL ||
         (uintptr_t)storage % _Alignof(max_align_t) != 0U))) {
        return A_STATUS_INVALID_PARAM;
    }
    if (binding->ready) return A_STATUS_BUSY;
    aStatus_t status = storage_layout(config, count, &nodes, &stride, &size);
    if (status != A_STATUS_OK) return status;
    binding->subscriptions = storage;
    binding->subscription_capacity = count;
    const size_t align = _Alignof(max_align_t);
    size_t requests = (count * sizeof(aCommBindingSubscription_t) +
        align - 1U) / align * align;
    binding->requests = config->request_capacity == 0U ? NULL :
        (void *)((uint8_t *)storage + requests);
    binding->request_capacity = config->request_capacity;
    for (size_t i = 0U; i < count; i++) {
        binding->subscriptions[i].native = (uint8_t *)storage + nodes +
            i * stride;
    }
    return A_STATUS_OK;
}
