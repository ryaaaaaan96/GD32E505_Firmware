#include "aModbus_mapping_internal.h"
#include <string.h>
typedef union {
    max_align_t alignment;
    uint8_t bytes[ACOMM_VALUE_CAPACITY];
} value_buffer_t;
aModbusMapArea_t aModbusMappingArea(uint16_t area)
{
    aModbusMapArea_t info = {0};
    (void)aModbusMapArea(area, &info);
    return info;
}

static aBool_t area_valid(uint16_t area)
{
    aModbusMapArea_t info = aModbusMappingArea(area);
    return info.unit_bits != 0U && (info.unit_bits == 1U ||
        info.unit_bits % 8U == 0U) && info.address_limit <= 65536U;
}

static size_t unit_size(uint16_t area)
{
    return aModbusMappingArea(area).unit_bits / 8U;
}

static aBool_t area_bits(uint16_t area)
{
    return aModbusMappingArea(area).unit_bits == 1U;
}

aStatus_t aModbusMappingAccessCheck(const aModbusMappingConfig_t *config,
    uint8_t unit, uint16_t area, uint16_t address, uint16_t quantity,
    const void *data, size_t size, aBool_t write)
{
    const aModbusMapAddress_t target = {unit, area, address, quantity};
    aModbusMapArea_t info = aModbusMappingArea(area);
    if (!area_valid(area) || quantity == 0U ||
        data == NULL) return A_STATUS_INVALID_PARAM;
    size_t bytes = info.unit_bits == 1U ? (quantity + 7U) / 8U :
        (size_t)quantity * (info.unit_bits / 8U);
    if (bytes > size || bytes > ACOMM_VALUE_CAPACITY) {
        return A_STATUS_INVALID_PARAM;
    }
    return aModbusMapCheck(config->protocol,
        &target, data, size, write);
}

static aStatus_t target_info(const aModbusMappingConfig_t *config,
    const aCommTarget_t *target, uint16_t area,
    uint8_t order, aModbusMapValue_t *info)
{
    const aCommDataQuery_t query = {target->deviceID, target->sigIndex};
    aCommDataInfo_t sig;
    aStatus_t status = aCommDataInfo(config->comm.data, &query, &sig);
    if (status != A_STATUS_OK) return status;
    status = aModbusMapDescribe(&sig, target, area, order, info);
    if (status == A_STATUS_OK &&
        (info->size == 0U || info->size > ACOMM_VALUE_CAPACITY)) {
        return A_STATUS_INVALID_PARAM;
    }
    return status;
}

aStatus_t aModbusMappingDataTargetInfo(aModbusMapping_t *handle,
    const aCommTarget_t *target, uint16_t area,
    uint8_t order, aModbusMapValue_t *info)
{
    aStatus_t status = target_info(handle->config, target,
                                    area, order, info);
    /* 从站映射已在初始化检查；主站可临时提交任意目标，需逐请求检查。 */
    if (status == A_STATUS_OK && handle->role == AMODBUS_MAPPING_CLIENT) {
        return aModbusMapLayout(info);
    }
    return status;
}

aStatus_t aModbusMappingClientSigCheck(const aModbusMappingConfig_t *config,
    const aModbusMappingClientSigRequest_t *request, aBool_t write)
{
    aModbusMapValue_t info;
    aStatus_t status;
    max_align_t placeholder = {0};
    if (config == NULL || config->comm.data == NULL || request == NULL ||
        !aTimeoutIsValid(request->timeout)) return A_STATUS_INVALID_PARAM;
    status = aModbusMappingConfigCheck(config);
    if (status != A_STATUS_OK) return status;
    status = target_info(config, &request->target, request->area,
                        request->word_order, &info);
    if (status != A_STATUS_OK) return status;
    status = aModbusMapLayout(&info);
    if (status != A_STATUS_OK) return status;
    if (!write && config->comm.sig_write == NULL) {
        aCommDataInfo_t sig;
        const aCommDataQuery_t query = {
            request->target.deviceID, request->target.sigIndex
        };
        size_t capacity = config->comm.snapshot == NULL ?
            ACOMM_VALUE_CAPACITY : config->comm.snapshot_size;
        status = aCommDataInfo(config->comm.data, &query, &sig);
        if (status != A_STATUS_OK) return status;
        if (sig.size > capacity) return A_STATUS_INVALID_PARAM;
    }
    /* AccessCheck 只检查地址、容量及对齐，不解引用缓冲区。 */
    return aModbusMappingAccessCheck(config, request->unit_id,
        request->area, request->address,
        info.quantity, &placeholder, ACOMM_VALUE_CAPACITY, write);
}

aStatus_t aModbusMappingClientMapCheck(const aModbusMappingConfig_t *config,
    const aModbusClientMap_t *map, uint8_t unit_id)
{
    aModbusMapValue_t info;
    aStatus_t status;
    if (map == NULL || map->quantity == 0U ||
        (map->comm.flags & ~ACOMM_MAP_FLAG_MASK) != 0U) {
        return A_STATUS_INVALID_PARAM;
    }
    if ((map->comm.poll_time_ms > INT32_MAX &&
         map->comm.poll_time_ms != ACOMM_POLL_ONCE) ||
        (map->comm.flags & ACOMM_MAP_FLAG_ACCESS_MASK) == 0U ||
        (map->comm.poll_time_ms != 0U &&
         (map->comm.flags & ACOMM_MAP_FLAG_READ) == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    status = aCommTargetConfigCheck(&AMODBUS_MAP_TARGET(map->comm));
    if (status != A_STATUS_OK) return status;
    /* 复用单项请求的类型/地址检查；此处只校验，不执行或等待事务。 */
    const aModbusMappingClientSigRequest_t request = {
        .unit_id = unit_id, .area = map->area, .address = map->address,
        .target = ACOMM_MAP_LOCATION(map->comm),
        .word_order = map->word_order,
        .timeout = A_TIMEOUT_NO_WAIT
    };
    status = aModbusMappingClientSigCheck(config, &request,
        (map->comm.flags & ACOMM_MAP_FLAG_READ) == 0U);
    if (status == A_STATUS_OK &&
        (map->comm.flags & ACOMM_MAP_FLAG_WRITE) != 0U) {
        status = aModbusMappingClientSigCheck(config, &request, A_TRUE);
    }
    if (status != A_STATUS_OK) return status;
    status = target_info(config, &ACOMM_MAP_LOCATION(map->comm),
                         map->area, map->word_order, &info);
    if (status != A_STATUS_OK) return status;
    /* 显式长度是协议配置；目标类型改变后必须重新核对，不能静默扩宽读取。 */
    return map->quantity == info.quantity ? A_STATUS_OK :
           A_STATUS_INVALID_PARAM;
}
/* 区域决定基础读写能力；每个回调段和离散映射独立声明权限。 */
static aBool_t permissions_valid(uint16_t area, uint16_t flags)
{
    return flags != 0U &&
        (flags & ~(ACOMM_MAP_FLAG_READ |
                   ACOMM_MAP_FLAG_WRITE)) == 0U &&
        (aModbusMappingArea(area).writable ||
         (flags & ACOMM_MAP_FLAG_WRITE) == 0U);
}

aStatus_t aModbusMappingDataDefinitionsCheck(
    const aModbusMappingServerConfig_t *config)
{
    uint32_t previous_end = 0U;
    uint16_t previous_area = 0U;
    size_t range_index = 0U;
    if ((config->ranges == NULL) != (config->range_count == 0U) ||
        (config->maps == NULL) != (config->map_count == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    /* 先检查回调段的边界、排序、权限及必需回调。 */
    for (size_t i = 0U; i < config->range_count; i++) {
        const aModbusMappingAddressRange_t *range = &config->ranges[i];
        uint32_t end;
        if (!area_valid(range->area) ||
            range->quantity == 0U ||
            range->quantity > aModbusMappingArea(range->area).address_limit ||
            !permissions_valid(range->area, range->flags)) {
            return A_STATUS_INVALID_PARAM;
        }
        end = (uint32_t)range->address + range->quantity;
        if (end > aModbusMappingArea(range->area).address_limit || (i != 0U &&
            (range->area < previous_area ||
             (range->area == previous_area &&
              range->address < previous_end))) ||
            ((range->flags & ACOMM_MAP_FLAG_READ) != 0U &&
             range->read == NULL) ||
            ((range->flags & ACOMM_MAP_FLAG_WRITE) != 0U &&
             range->write == NULL)) {
            return A_STATUS_INVALID_PARAM;
        }
        previous_area = range->area;
        previous_end = end;
    }
    previous_area = 0U;
    previous_end = 0U;
    /* 校验显式协议长度与数据端点目标匹配，再检查排序和区间重叠。 */
    for (size_t i = 0U; i < config->map_count; i++) {
        const aModbusServerMap_t *map = &config->maps[i];
        aModbusMapValue_t info;
        aStatus_t status;
        uint32_t end;
        status = aCommTargetConfigCheck(&AMODBUS_MAP_TARGET(map->comm));
        if (status != A_STATUS_OK) return status;
        /* 从站只能观察变化，不能成为主动向远端读写的处理者。 */
        uint32_t mask = map->comm.subscribe_events;
        if ((mask != ACOMM_SUBSCRIBE_MARK_ONLY &&
             (mask & ~(ACOMM_EVENT_UPDATED | ACOMM_EVENT_WRITE_COMPLETE)) != 0U)
            || map->comm.publish_events != 0U) return A_STATUS_UNSUPPORTED;
        if (mask != 0U && mask != ACOMM_SUBSCRIBE_MARK_ONLY &&
            config->binding.comm.data->ops->data->subscribe != NULL &&
            config->notify == NULL) return A_STATUS_INVALID_PARAM;
        if (!area_valid(map->area) ||
            map->quantity == 0U ||
            !permissions_valid(map->area, map->comm.flags)) {
            return A_STATUS_INVALID_PARAM;
        }
        status = target_info(&config->binding,
            &ACOMM_MAP_LOCATION(map->comm),
                             map->area, map->word_order, &info);
        if (status != A_STATUS_OK) return status;
        status = aModbusMapLayout(&info);
        if (status != A_STATUS_OK) return status;
        if (map->quantity != info.quantity) return A_STATUS_INVALID_PARAM;
        end = (uint32_t)map->address + map->quantity;
        if (end > aModbusMappingArea(map->area).address_limit || (i != 0U &&
            (map->area < previous_area ||
             (map->area == previous_area && map->address < previous_end))) ||
            ((map->comm.flags & ACOMM_MAP_FLAG_WRITE) != 0U &&
             map->quantity > aModbusMappingArea(map->area).write_limit)) {
            return A_STATUS_INVALID_PARAM;
        }
        previous_area = map->area;
        previous_end = end;
        /* 两张有序表只向前遍历，检查段与映射的完整区间是否相交。 */
        while (range_index < config->range_count) {
            const aModbusMappingAddressRange_t *range =
                &config->ranges[range_index];
            if (range->area < map->area ||
                (range->area == map->area &&
                 (uint32_t)range->address + range->quantity <= map->address)) {
                range_index++;
                continue;
            }
            if (range->area == map->area && range->address < end) {
                return A_STATUS_INVALID_PARAM;
            }
            break;
        }
        if (((map->comm.flags & ACOMM_MAP_FLAG_READ) != 0U &&
             config->binding.comm.sig_read == NULL) ||
            ((map->comm.flags & ACOMM_MAP_FLAG_WRITE) != 0U &&
             config->binding.comm.sig_write == NULL)) {
            aCommDataInfo_t sig;
            const aCommDataQuery_t query = {
                map->comm.deviceID, map->comm.sigIndex
            };
            size_t capacity = config->binding.comm.snapshot == NULL ?
                ACOMM_VALUE_CAPACITY : config->binding.comm.snapshot_size;
            status = aCommDataInfo(config->binding.comm.data, &query, &sig);
            if (status != A_STATUS_OK) return status;
            if (sig.size > capacity) return A_STATUS_INVALID_PARAM;
        }
    }
    return A_STATUS_OK;
}

aStatus_t aModbusMappingValueDecode(const aModbusMapValue_t *info,
    uint16_t area, uint8_t order,
    const void *protocol, void *value)
{
    return aModbusMapCopy(info, area, order,
        0U, info->quantity, value, (void *)protocol, A_TRUE);
}

aStatus_t aModbusMappingValueEncode(const aModbusMapValue_t *info,
    uint16_t area, uint8_t order,
    const void *value, void *protocol)
{
    return aModbusMapCopy(info, area, order,
        0U, info->quantity, (void *)value, protocol, A_FALSE);
}
/* 定义表按区域和地址排序；二分找到起点之前的最后一段/映射。 */
static const aModbusMappingAddressRange_t *range_find(
    aModbusMapping_t *handle,
    uint16_t area, uint32_t address)
{
    size_t first = 0U;
    size_t last = handle->server.range_count;
    while (first < last) {
        size_t middle = first + (last - first) / 2U;
        const aModbusMappingAddressRange_t *range =
            &handle->server.ranges[middle];
        if (range->area < area ||
            (range->area == area && range->address <= address)) {
            first = middle + 1U;
        } else {
            last = middle;
        }
    }
    if (first != 0U) {
        const aModbusMappingAddressRange_t *range =
            &handle->server.ranges[first - 1U];
        if (range->area == area &&
            address < (uint32_t)range->address + range->quantity) {
            return range;
        }
    }
    return NULL;
}
/* 返回首个大于查询地址的映射下标；前一项可能覆盖查询起点。 */
static size_t map_upper_bound(const aModbusMappingServerConfig_t *config,
    uint16_t area, uint32_t address)
{
    size_t first = 0U;
    size_t last = config->map_count;
    while (first < last) {
        size_t middle = first + (last - first) / 2U;
        const aModbusServerMap_t *map = &config->maps[middle];
        if (map->area < area ||
            (map->area == area && map->address <= address)) {
            first = middle + 1U;
        } else {
            last = middle;
        }
    }
    return first;
}

static const aModbusServerMap_t *map_find(aModbusMapping_t *handle,
    uint16_t area, uint32_t address)
{
    size_t after = map_upper_bound(&handle->server, area, address);
    if (after != 0U && handle->server.maps[after - 1U].area == area) {
        return &handle->server.maps[after - 1U];
    }
    return NULL;
}

static uint16_t segment_size(uint32_t address, uint32_t end,
                             uint16_t remaining)
{
    uint32_t size = end - address;
    return size < remaining ? (uint16_t)size : remaining;
}
/* 部分整组写入必须在字段边界开始和结束，禁止半值读改写。 */
static aStatus_t map_write_check(aModbusMapping_t *handle,
    const aModbusServerMap_t *map, uint32_t at, uint16_t count)
{
    aModbusMapValue_t info;
    aStatus_t status;
    size_t first = (at - map->address) *
        unit_size(map->area);
    size_t end = first + count *
        unit_size(map->area);
    aBool_t start_found = A_FALSE;
    aBool_t end_found = A_FALSE;
    if (at == map->address && count == map->quantity) return A_STATUS_OK;
    status = aModbusMappingDataTargetInfo(handle,
        &ACOMM_MAP_LOCATION(map->comm), map->area, map->word_order, &info);
    if (status != A_STATUS_OK) return status;
    if (info.type != ALIB_DATA_STRUCT) return A_STATUS_NOT_FOUND;
    for (size_t i = 0U; i < info.param_count; i++) {
        const aDataField_t *param = &info.params[i];
        if (param->offset == first) start_found = A_TRUE;
        if (param->offset + param->size == end) end_found = A_TRUE;
    }
    return start_found && end_found ? A_STATUS_OK : A_STATUS_NOT_FOUND;
}
/* 第一遍只检查路由；不调用业务回调、不修改数据端点或读输出。 */
static aStatus_t route_check(aModbusMapping_t *handle, uint16_t area,
    uint16_t address, uint16_t quantity, aBool_t write)
{
    uint32_t at = address;
    uint16_t left = quantity;
    uint16_t permission = write ? ACOMM_MAP_FLAG_WRITE :
                                  ACOMM_MAP_FLAG_READ;
    while (left != 0U) {
        const aModbusMappingAddressRange_t *range =
            range_find(handle, area, at);
        uint16_t count;
        if (range != NULL) {
            if ((range->flags & permission) == 0U) return A_STATUS_NOT_FOUND;
            count = segment_size(at, (uint32_t)range->address +
                                  range->quantity, left);
        } else {
            const aModbusServerMap_t *map = map_find(handle, area, at);
            if (map == NULL || (map->comm.flags & permission) == 0U) {
                return A_STATUS_NOT_FOUND;
            }
            /* 初始化已验证长度；路由检查直接使用配置，避免查询 数据端点。 */
            if (at >= (uint32_t)map->address + map->quantity) {
                return A_STATUS_NOT_FOUND;
            }
            count = segment_size(at, (uint32_t)map->address +
                                  map->quantity, left);
            if (write) {
                aStatus_t status = map_write_check(handle, map, at, count);
                if (status != A_STATUS_OK) return status;
            }
        }
        at += count;
        left = (uint16_t)(left - count);
    }
    return A_STATUS_OK;
}
/* 帧内同一 SIG 统一取快照或提交，非连续映射也属于同一组。
 * 跨 SIG 仍独立处理；用户自定义回调自行决定同步及事务边界。
 */
static aBool_t same_sig(const aCommEntry_t *a, const aCommEntry_t *b)
{
    return a->deviceID == b->deviceID && a->sigIndex == b->sigIndex;
}
static aBool_t group_seen(aModbusMapping_t *handle, uint32_t start,
    const aModbusServerMap_t *map)
{
    size_t previous = handle->links[map - handle->server.maps].previous;
    if (previous == SIZE_MAX) return A_FALSE;
    const aModbusServerMap_t *before = &handle->server.maps[previous];
    return (uint32_t)before->address + before->quantity > start;
}
typedef struct {
    aModbusMapping_t *handle;
    uint16_t area;
    uint32_t start;
    uint32_t end;
    void *protocol;
    size_t first;
    aCommDataInfo_t sig;
    aBool_t write;
} group_access_t;
static aStatus_t group_copy(void *context, void *snapshot, size_t size)
{
    group_access_t *group = context;
    aModbusMapping_t *handle = group->handle;
    (void)size;
    /* 初始化已串起同一 SIG；帧内只访问本组成员，避免重复扫描点表。 */
    for (size_t m = group->first; m != SIZE_MAX; m = handle->links[m].next) {
        const aModbusServerMap_t *map = &handle->server.maps[m];
        aModbusMapValue_t info;
        aStatus_t status;
        uint32_t at, end;
        size_t count, offset;
        uint8_t bit = 0U;
        void *value;
        void *protocol;
        if (map->area < group->area) continue;
        if (map->area > group->area || map->address >= group->end) break;
        status = aModbusMapDescribe(&group->sig,
            &ACOMM_MAP_LOCATION(map->comm), group->area,
            map->word_order, &info);
        if (status != A_STATUS_OK) return status;
        at = map->address > group->start ? map->address : group->start;
        end = (uint32_t)map->address + map->quantity;
        if (end > group->end) end = group->end;
        if (at >= end) continue;
        count = end - at;
        offset = at - group->start;
        value = snapshot;
        if (map->comm.paramIndex != ACOMM_MAP_SIG_WHOLE) {
            value = (uint8_t *)snapshot +
                group->sig.params[map->comm.paramIndex].offset;
        }
        if (area_bits(group->area)) {
            if (group->write) {
                bit = bit_read(
                    (uint8_t *)group->protocol, offset);
            }
            protocol = &bit;
        } else {
            protocol = (uint8_t *)group->protocol + offset *
                unit_size(group->area);
        }
        if (!area_bits(group->area)) {
            status = aModbusMapCopy(&info, map->area, map->word_order,
                at - map->address, count, value, protocol, group->write);
        } else if (group->write) {
            status = aModbusMappingValueDecode(&info, group->area,
                map->word_order, protocol, value);
        } else {
            status = aModbusMappingValueEncode(&info, group->area,
                map->word_order, value, &bit);
            if (status == A_STATUS_OK) {
                bit_write((uint8_t *)group->protocol, offset, bit != 0U);
            }
        }
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
}

static aStatus_t group_transfer(aModbusMapping_t *handle,
    uint16_t area, uint16_t address, uint16_t quantity, void *protocol,
    const aModbusServerMap_t *map, aBool_t write)
{
    const aCommTarget_t target = ACOMM_MAP_LOCATION(map->comm);
    group_access_t group = {
        .handle = handle, .area = area, .start = address,
        .end = (uint32_t)address + quantity, .protocol = protocol,
        .first = (size_t)(map - handle->server.maps), .write = write
    };
    aCommDataQuery_t query = {target.deviceID, target.sigIndex};
    aStatus_t status = aCommDataInfo(handle->config->comm.data, &query,
        &group.sig);
    if (status != A_STATUS_OK) return status;
    const aCommBindingTransfer_t transfer = {
        .target = query, .size = group.sig.size,
        .edit = group_copy, .context = &group, .write = write,
        .timeout = aModbusMappingRemaining(handle)
    };
    return aCommBindingTransfer(&handle->comm, &transfer);
}

aStatus_t aModbusMappingDataRead(aModbusMapping_t *handle,
    const aModbusAddressReadRequest_t *request)
{
    uint32_t at = request->address;
    uint16_t left = request->quantity;
    aStatus_t status = aModbusMappingAccessCheck(handle->config, 1U,
        request->area, request->address,
        request->quantity, request->data, request->size, A_FALSE);
    if (status != A_STATUS_OK) return status;
    status = route_check(handle, request->area, request->address,
                          request->quantity, A_FALSE);
    if (status != A_STATUS_OK) return status;
    if (area_bits(request->area)) {
        memset(request->data, 0, (request->quantity + 7U) / 8U);
    }
    while (left != 0U) {
        const aModbusMappingAddressRange_t *range =
            range_find(handle, request->area, at);
        size_t offset = at - request->address;
        uint16_t count;
        uint8_t bits[ACOMM_VALUE_CAPACITY] = {0};
        if (range != NULL) {
            aModbusAddressReadRequest_t part = *request;
            count = segment_size(at, (uint32_t)range->address +
                                  range->quantity, left);
            part.address = (uint16_t)at;
            part.quantity = count;
            part.data = area_bits(request->area) ?
                (void *)bits :
                        (uint8_t *)request->data + offset *
                        unit_size(request->area);
            part.size = area_bits(request->area) ?
                        (count + 7U) / 8U : count *
                        unit_size(request->area);
            part.timeout = aModbusMappingRemaining(handle);
            status = range->read(range->context, &part);
        } else {
            const aModbusServerMap_t *map =
                map_find(handle, request->area, at);
            aModbusMapValue_t info;
            value_buffer_t value;
            count = segment_size(at, (uint32_t)map->address +
                                  map->quantity, left);
            if (handle->config->comm.sig_read == NULL) {
                if (!group_seen(handle, request->address, map)) {
                    status = group_transfer(handle, request->area,
                        request->address, request->quantity, request->data,
                        map, A_FALSE);
                    if (status != A_STATUS_OK) return status;
                }
                at += count;
                left = (uint16_t)(left - count);
                continue;
            }
            status = aModbusMappingDataTargetInfo(handle,
                &ACOMM_MAP_LOCATION(map->comm),
                request->area, map->word_order, &info);
            if (status != A_STATUS_OK) return status;
            status = aCommBindingRead(&handle->comm,
                &(aCommValueReadRequest_t){
                    .target = ACOMM_MAP_LOCATION(map->comm),
                    .data = value.bytes,
                    .size = info.size,
                    .timeout = aModbusMappingRemaining(handle)
                });
            if (status != A_STATUS_OK) return status;
            if (area_bits(request->area)) {
                status = aModbusMappingValueEncode(&info, request->area,
                    map->word_order, value.bytes, bits);
            } else {
                status = aModbusMapCopy(&info, map->area, map->word_order,
                    at - map->address, count, value.bytes,
                    (uint8_t *)request->data + offset *
                        unit_size(request->area),
                    A_FALSE);
            }
        }
        if (status != A_STATUS_OK) return status;
        if (area_bits(request->area)) {
            uint8_t *out = request->data;
            for (size_t i = 0U; i < count; i++) {
                bit_write(out, offset + i,
                                    bit_read(bits, i));
            }
        }
        at += count;
        left = (uint16_t)(left - count);
    }
    return A_STATUS_OK;
}
/* 自定义整组回调在完整写入时调用一次；部分写入按完整字段回调。
 * 不读取业务旧值来合成整组，回调提供者自行保证多字段同步和失败语义。
 */
static aStatus_t map_callback_write(aModbusMapping_t *handle,
    const aModbusServerMap_t *map, const aModbusMapValue_t *info,
    size_t first, size_t count, const void *value)
{
    if (info->type != ALIB_DATA_STRUCT ||
        (first == 0U && count == info->quantity)) {
        return aCommBindingWrite(&handle->comm,
            &(aCommValueWriteRequest_t){
                .target = ACOMM_MAP_LOCATION(map->comm), .data = value,
                .size = info->size,
                .timeout = aModbusMappingRemaining(handle)
            });
    }
    for (size_t i = 0U; i < info->param_count; i++) {
        const aDataField_t *param = &info->params[i];
        aCommTarget_t target =
            ACOMM_MAP_LOCATION(map->comm);
        aStatus_t status;
        if (param->offset < first *
            unit_size(map->area)) continue;
        if (param->offset >= (first + count) *
            unit_size(map->area)) break;
        target.paramIndex = i;
        status = aCommBindingWrite(&handle->comm,
            &(aCommValueWriteRequest_t){
                .target = target,
                .data = (const uint8_t *)value + param->offset,
                .size = param->size,
                .timeout = aModbusMappingRemaining(handle)
            });
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
}
/* 两遍处理写入：先检查所有映射值，再逐项提交。
 * 第二遍的锁/业务回调仍可能失败，跨 SIG 不提供事务性或回滚。
 */
static aStatus_t write_pass(aModbusMapping_t *handle,
    const aModbusAddressWriteRequest_t *request, aBool_t apply)
{
    uint32_t at = request->address;
    uint16_t left = request->quantity;
    while (left != 0U) {
        const aModbusMappingAddressRange_t *range =
            range_find(handle, request->area, at);
        size_t offset = at - request->address;
        uint16_t count;
        uint8_t bits[ACOMM_VALUE_CAPACITY] = {0};
        aStatus_t status = A_STATUS_OK;
        if (range != NULL) {
            aModbusAddressWriteRequest_t part = *request;
            count = segment_size(at, (uint32_t)range->address +
                                  range->quantity, left);
            if (apply) {
                part.address = (uint16_t)at;
                part.quantity = count;
                if (area_bits(request->area)) {
                    const uint8_t *input = request->data;
                    for (size_t i = 0U; i < count; i++) {
                        bit_write(bits, i,
                            bit_read(input, offset + i));
                    }
                    part.data = bits;
                    part.size = (count + 7U) / 8U;
                } else {
                    part.data = (const uint8_t *)request->data + offset *
                        unit_size(request->area);
                    part.size = count *
                        unit_size(request->area);
                }
                part.timeout = aModbusMappingRemaining(handle);
                status = range->write(range->context, &part);
            }
        } else {
            const aModbusServerMap_t *map =
                map_find(handle, request->area, at);
            aModbusMapValue_t info;
            value_buffer_t value;
            const void *protocol;
            status = aModbusMappingDataTargetInfo(handle,
                &ACOMM_MAP_LOCATION(map->comm),
                request->area, map->word_order, &info);
            if (status != A_STATUS_OK) return status;
            count = segment_size(at, (uint32_t)map->address +
                                  map->quantity, left);
            if (area_bits(request->area)) {
                const uint8_t *input = request->data;
                bits[0] = bit_read(input, offset) ? 1U : 0U;
                protocol = bits;
            } else {
                protocol = (const uint8_t *)request->data + offset *
                        unit_size(request->area);
            }
            if (area_bits(request->area)) {
                status = aModbusMappingValueDecode(&info, request->area,
                    map->word_order, protocol, value.bytes);
            } else {
                status = aModbusMapCopy(&info, map->area, map->word_order,
                    at - map->address, count, value.bytes,
                    (void *)protocol, A_TRUE);
            }
            if (status == A_STATUS_OK && apply) {
                if (handle->config->comm.sig_write == NULL) {
                    if (!group_seen(handle, request->address, map)) {
                        status = group_transfer(handle, request->area,
                            request->address, request->quantity,
                            (void *)request->data,
                            map, A_TRUE);
                    }
                } else {
                    status = map_callback_write(handle, map, &info,
                        at - map->address, count, value.bytes);
                }
            }
        }
        if (status != A_STATUS_OK) return status;
        at += count;
        left = (uint16_t)(left - count);
    }
    return A_STATUS_OK;
}

aStatus_t aModbusMappingDataWrite(aModbusMapping_t *handle,
    const aModbusAddressWriteRequest_t *request)
{
    aStatus_t status = aModbusMappingAccessCheck(handle->config, 1U,
        request->area, request->address,
        request->quantity, request->data, request->size, A_TRUE);
    if (status != A_STATUS_OK) return status;
    status = route_check(handle, request->area, request->address,
                          request->quantity, A_TRUE);
    if (status != A_STATUS_OK) return status;
    status = write_pass(handle, request, A_FALSE);
    if (status != A_STATUS_OK) return status;
    return write_pass(handle, request, A_TRUE);
}
/* 一次远端读取可覆盖多个 SIG，提交前先检查所有目标值。 */
typedef struct {
    aModbusMapping_t *handle;
    const aModbusClientMap_t *items;
    const size_t *indices;
    size_t count;
    size_t selected;
    size_t map_offset; /**< 独立临时表无缓存时为 SIZE_MAX。 */
    const void *data;
    aCommDataInfo_t sig;
} publish_context_t;
static const aModbusClientMap_t *publish_item(
    const publish_context_t *publish, size_t index)
{
    return &publish->items[publish->indices[index]];
}
/* 按帧中的地址偏移解码，只读表无需复制到 RAM。 */
static aStatus_t publish_decode(const publish_context_t *publish,
    const aModbusClientMap_t *item,
    const aModbusMapValue_t *info,
    void *value)
{
    size_t offset = item->address - publish_item(publish, 0U)->address;
    uint8_t bit;
    const void *protocol;
    if (area_bits(item->area)) {
        bit = bit_read((const uint8_t *)publish->data, offset);
        protocol = &bit;
    } else {
        protocol = (const uint8_t *)publish->data + offset *
            unit_size(item->area);
    }
    return aModbusMappingValueDecode(info, item->area,
        item->word_order,
        protocol, value);
}

static aStatus_t publish_edit(void *context, void *snapshot, size_t size)
{
    publish_context_t *publish = context;
    const aBool_t cached = publish->map_offset != SIZE_MAX;
    size_t cursor = cached ? publish->selected + publish->map_offset : 0U;
    size_t end = cached ? publish->map_offset +
        publish->indices[publish->count - 1U] + 1U : publish->count;
    (void)size;
    while (cursor < end) {
        size_t index = cached ? cursor - publish->map_offset :
            publish->indices[cursor];
        cursor = cached ? publish->handle->links[cursor].next : cursor + 1U;
        const aModbusClientMap_t *item = &publish->items[index];
        if (!cached && !same_sig(&item->comm,
            &publish->items[publish->selected].comm)) continue;
        aModbusMapValue_t info;
        aStatus_t status;
        void *value = snapshot;
        status = aModbusMapDescribe(&publish->sig,
            &ACOMM_MAP_LOCATION(item->comm), item->area,
            item->word_order, &info);
        if (status != A_STATUS_OK) return status;
        if (item->comm.paramIndex != ACOMM_MAP_SIG_WHOLE) {
            value = (uint8_t *)snapshot +
                publish->sig.params[item->comm.paramIndex].offset;
        }
        status = publish_decode(publish, item, &info, value);
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
}

static aStatus_t publish_pass(aModbusMapping_t *handle,
    publish_context_t *publish, aBool_t apply)
{
    for (size_t i = 0U; i < publish->count; i++) {
        const aModbusClientMap_t *item = publish_item(publish, i);
        const aCommTarget_t *target =
            &ACOMM_MAP_LOCATION(item->comm);
        aCommDataQuery_t query = {target->deviceID, target->sigIndex};
        aStatus_t status;
        if (!apply || handle->config->comm.sig_write != NULL) {
            aModbusMapValue_t info;
            value_buffer_t value;
            status = aModbusMappingDataTargetInfo(handle, target, item->area,
                                          item->word_order, &info);
            if (status != A_STATUS_OK) return status;
            status = publish_decode(publish, item, &info, value.bytes);
            if (status == A_STATUS_OK && apply) {
                status = aCommBindingWrite(&handle->comm,
                    &(aCommValueWriteRequest_t){
                        .target = *target, .data = value.bytes,
                        .size = info.size,
                        .timeout = aModbusMappingRemaining(handle)
                    });
            }
        } else {
            aBool_t seen = A_FALSE;
            if (publish->map_offset != SIZE_MAX) {
                size_t previous = handle->links[
                    publish->map_offset + publish->indices[i]].previous;
                seen = previous != SIZE_MAX && previous >=
                    publish->map_offset + publish->indices[0];
            } else {
                /* 独立计划可借用另一张临时表，不能使用注册表的下标。 */
                for (size_t j = 0U; j < i; j++) {
                    if (same_sig(&item->comm,
                                 &publish_item(publish, j)->comm)) {
                        seen = A_TRUE;
                        break;
                    }
                }
            }
            if (seen) continue;
            publish->selected = publish->indices[i];
            status = aCommDataInfo(handle->config->comm.data, &query,
                &publish->sig);
            if (status == A_STATUS_OK) {
                const aCommBindingTransfer_t transfer = {
                    .target = query, .size = publish->sig.size,
                    .edit = publish_edit, .context = publish, .write = A_TRUE,
                    .timeout = aModbusMappingRemaining(handle)
                };
                status = aCommBindingTransfer(&handle->comm, &transfer);
            }
        }
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
}

aStatus_t aModbusMappingDataPublish(aModbusMapping_t *handle,
    const aModbusClientMap_t *items, const size_t *indices, size_t count,
    const void *data)
{
    publish_context_t publish = {
        .handle = handle, .items = items, .indices = indices,
        .count = count, .data = data, .map_offset = SIZE_MAX
    };
    uintptr_t first = (uintptr_t)handle->client.maps;
    uintptr_t current = (uintptr_t)items;
    if (count != 0U && current >= first && handle->client.maps != NULL) {
        size_t bytes = current - first;
        size_t offset = bytes / sizeof(*items);
        if (bytes % sizeof(*items) == 0U && offset < handle->client.map_count &&
            indices[count - 1U] < handle->client.map_count - offset) {
            publish.map_offset = offset;
        }
    }
    /* 第一遍只校验，第二遍才提交；范围错误不能改写前面的 SIG。 */
    aStatus_t status = publish_pass(handle, &publish, A_FALSE);
    return status == A_STATUS_OK ?
        publish_pass(handle, &publish, A_TRUE) : status;
}
