#include "aMemory.h"
#include <string.h>

_Static_assert(AMEMORY_MAX_PARTITIONS > 0U &&
               AMEMORY_MAX_PARTITIONS <= 256U, "分区容量必须为 1..256");

struct aMemoryHandle {
    const aMemoryPartition_t *partition;
};

static aMemoryHandle_t handles[AMEMORY_MAX_PARTITIONS];
static size_t handle_count;
static size_t references;

void aMemoryConfigStructInit(aMemoryConfig_t *config)
{
    if (config == NULL) return;
    *config = (aMemoryConfig_t){
        .devices = NULL, .device_count = 0U,
        .partitions = NULL, .partition_count = 0U
    };
}

void aMemoryReadRequestStructInit(aMemoryReadRequest_t *request)
{
    if (request == NULL) return;
    *request = (aMemoryReadRequest_t){
        .address = 0U, .data = NULL, .size = 0U,
        .timeout = A_TIMEOUT_MS(5000U)
    };
}

void aMemoryWriteRequestStructInit(aMemoryWriteRequest_t *request)
{
    if (request == NULL) return;
    *request = (aMemoryWriteRequest_t){
        .address = 0U, .data = NULL, .size = 0U,
        .timeout = A_TIMEOUT_MS(5000U)
    };
}

void aMemoryEraseRequestStructInit(aMemoryEraseRequest_t *request)
{
    if (request == NULL) return;
    *request = (aMemoryEraseRequest_t){
        .address = 0U, .size = 0U, .timeout = A_TIMEOUT_MS(5000U)
    };
}

static aBool_t name_valid(const char *name)
{
    return name != NULL && name[0] != '\0';
}

static aBool_t device_valid(const aMemoryDevice_t *device)
{
    const aMemoryGeometry_t *geometry;

    if (device == NULL || !name_valid(device->name) || device->ops == NULL)
        return A_FALSE;
    geometry = &device->geometry;
    if (geometry->capacity == 0U || geometry->read_granularity == 0U ||
        geometry->write_granularity == 0U) return A_FALSE;
    if (device->ops->read == NULL && device->ops->write == NULL &&
        device->ops->erase == NULL) return A_FALSE;
    if (device->ops->write != NULL && geometry->program_bits == 0U)
        return A_FALSE;
    if (device->ops->erase != NULL &&
        (geometry->erase_granularity == 0U ||
         geometry->capacity % geometry->erase_granularity != 0U))
        return A_FALSE;
    return A_TRUE;
}

static aBool_t partition_valid(const aMemoryPartition_t *partition,
                              const aMemoryConfig_t *config)
{
    const aMemoryDevice_t *device;
    const aMemoryGeometry_t *geometry;
    size_t i;

    if (partition == NULL || !name_valid(partition->name) ||
        partition->size == 0U || partition->access == 0U ||
        (partition->access & ~AMEMORY_ACCESS_ALL) != 0U) return A_FALSE;
    device = partition->device;
    for (i = 0U; i < config->device_count; ++i)
        if (config->devices[i] == device) break;
    if (i == config->device_count) return A_FALSE;
    geometry = &device->geometry;
    if (partition->offset > geometry->capacity ||
        partition->size > geometry->capacity - partition->offset)
        return A_FALSE;
    if ((partition->access & AMEMORY_ACCESS_READ) != 0U &&
        (device->ops->read == NULL ||
         partition->offset % geometry->read_granularity != 0U ||
         partition->size % geometry->read_granularity != 0U))
        return A_FALSE;
    if ((partition->access & AMEMORY_ACCESS_WRITE) != 0U &&
        (device->ops->write == NULL ||
         partition->offset % geometry->write_granularity != 0U ||
         partition->size % geometry->write_granularity != 0U))
        return A_FALSE;
    if ((partition->access & AMEMORY_ACCESS_ERASE) != 0U &&
        (device->ops->erase == NULL ||
         partition->offset % geometry->erase_granularity != 0U ||
         partition->size % geometry->erase_granularity != 0U))
        return A_FALSE;
    return A_TRUE;
}

aStatus_t aMemoryInit(const aMemoryConfig_t *config)
{
    size_t i, j;

    if (handle_count != 0U) return A_STATUS_BUSY;
    if (config == NULL || config->devices == NULL ||
        config->device_count == 0U || config->partitions == NULL ||
        config->partition_count == 0U) return A_STATUS_INVALID_PARAM;
    if (config->partition_count > AMEMORY_MAX_PARTITIONS)
        return A_STATUS_NO_MEMORY;
    /* 先完整校验，再发布句柄，失败不留下部分注册状态。 */
    for (i = 0U; i < config->device_count; ++i) {
        if (!device_valid(config->devices[i])) return A_STATUS_INVALID_PARAM;
        for (j = 0U; j < i; ++j)
            if (strcmp(config->devices[i]->name,
                       config->devices[j]->name) == 0)
                return A_STATUS_INVALID_PARAM;
    }
    for (i = 0U; i < config->partition_count; ++i) {
        const aMemoryPartition_t *part = config->partitions[i];

        if (!partition_valid(part, config)) return A_STATUS_INVALID_PARAM;
        for (j = 0U; j < i; ++j) {
            const aMemoryPartition_t *other = config->partitions[j];

            if (strcmp(part->name, other->name) == 0)
                return A_STATUS_INVALID_PARAM;
            if (part->device == other->device &&
                part->offset < other->offset + other->size &&
                other->offset < part->offset + part->size)
                return A_STATUS_INVALID_PARAM;
        }
    }
    for (i = 0U; i < config->partition_count; ++i)
        handles[i].partition = config->partitions[i];
    handle_count = config->partition_count;
    return A_STATUS_OK;
}

aStatus_t aMemoryDeInit(void)
{
    if (handle_count == 0U) return A_STATUS_NOT_READY;
    if (references != 0U) return A_STATUS_BUSY;
    memset(handles, 0, sizeof(handles));
    handle_count = 0U;
    return A_STATUS_OK;
}

aStatus_t aMemoryRetain(void)
{
    if (handle_count == 0U) return A_STATUS_NOT_READY;
    if (references == SIZE_MAX) return A_STATUS_BUSY;
    ++references;
    return A_STATUS_OK;
}

aStatus_t aMemoryRelease(void)
{
    if (handle_count == 0U || references == 0U) return A_STATUS_NOT_READY;
    --references;
    return A_STATUS_OK;
}

const aMemoryHandle_t *aMemoryFind(const char *name)
{
    if (!name_valid(name)) return NULL;
    for (size_t i = 0U; i < handle_count; ++i)
        if (strcmp(handles[i].partition->name, name) == 0)
            return &handles[i];
    return NULL;
}

static aStatus_t handle_check(const aMemoryHandle_t *handle)
{
    uintptr_t address = (uintptr_t)handle;
    uintptr_t base = (uintptr_t)handles;

    if (handle_count == 0U) return A_STATUS_NOT_READY;
    /* 通过注册表地址范围验证，正常读写不遍历设备或分区表。 */
    if (handle == NULL || address < base ||
        address - base >= handle_count * sizeof(handles[0]) ||
        (address - base) % sizeof(handles[0]) != 0U)
        return A_STATUS_INVALID_PARAM;
    return A_STATUS_OK;
}

aStatus_t aMemoryGetInfo(const aMemoryHandle_t *handle, aMemoryInfo_t *info)
{
    aStatus_t status = handle_check(handle);
    const aMemoryPartition_t *part;

    if (status != A_STATUS_OK) return status;
    if (info == NULL) return A_STATUS_INVALID_PARAM;
    part = handle->partition;
    info->name = part->name;
    info->device_name = part->device->name;
    info->offset = part->offset;
    info->geometry = part->device->geometry;
    info->geometry.capacity = part->size;
    info->access = part->access;
    return A_STATUS_OK;
}

static aStatus_t access_check(const aMemoryHandle_t *handle,
                             uint64_t address, size_t size,
                             aTimeout_t timeout, uint32_t access)
{
    aStatus_t status = handle_check(handle);
    const aMemoryPartition_t *part;
    const aMemoryGeometry_t *geometry;
    size_t granularity;

    if (status != A_STATUS_OK) return status;
    part = handle->partition;
    geometry = &part->device->geometry;
    if (!aTimeoutIsValid(timeout) || address > part->size ||
        size > part->size - address) return A_STATUS_INVALID_PARAM;
    if ((part->access & access) == 0U) return A_STATUS_UNSUPPORTED;
    if (access == AMEMORY_ACCESS_READ)
        granularity = geometry->read_granularity;
    else if (access == AMEMORY_ACCESS_WRITE)
        granularity = geometry->write_granularity;
    else
        granularity = geometry->erase_granularity;
    if (address % granularity != 0U || size % granularity != 0U)
        return A_STATUS_INVALID_PARAM;
    return A_STATUS_OK;
}

aStatus_t aMemoryRead(
    const aMemoryHandle_t *handle, const aMemoryReadRequest_t *request)
{
    aMemoryReadRequest_t device_request;
    aStatus_t status;

    if (request == NULL || (request->size != 0U && request->data == NULL))
        return A_STATUS_INVALID_PARAM;
    status = access_check(handle, request->address, request->size,
                          request->timeout, AMEMORY_ACCESS_READ);
    if (status != A_STATUS_OK || request->size == 0U) return status;
    device_request = *request;
    device_request.address += handle->partition->offset;
    return handle->partition->device->ops->read(
        handle->partition->device->context, &device_request);
}

aStatus_t aMemoryWrite(
    const aMemoryHandle_t *handle, const aMemoryWriteRequest_t *request)
{
    aMemoryWriteRequest_t device_request;
    aStatus_t status;

    if (request == NULL || (request->size != 0U && request->data == NULL))
        return A_STATUS_INVALID_PARAM;
    status = access_check(handle, request->address, request->size,
                          request->timeout, AMEMORY_ACCESS_WRITE);
    if (status != A_STATUS_OK || request->size == 0U) return status;
    device_request = *request;
    device_request.address += handle->partition->offset;
    return handle->partition->device->ops->write(
        handle->partition->device->context, &device_request);
}

aStatus_t aMemoryErase(
    const aMemoryHandle_t *handle, const aMemoryEraseRequest_t *request)
{
    aMemoryEraseRequest_t device_request;
    aStatus_t status;

    if (request == NULL) return A_STATUS_INVALID_PARAM;
    status = access_check(handle, request->address, request->size,
                          request->timeout, AMEMORY_ACCESS_ERASE);
    if (status != A_STATUS_OK || request->size == 0U) return status;
    device_request = *request;
    device_request.address += handle->partition->offset;
    return handle->partition->device->ops->erase(
        handle->partition->device->context, &device_request);
}
