#include "aDataBase.h"
#include "fal.h"
#include <assert.h>
#include <string.h>

static unsigned calls;
static aStatus_t storage_read(void *context, uint32_t address, uint8_t *buffer,
                              uint32_t size, aTimeout_t timeout)
{
    assert(context == &calls && address == 0x100000U);
    assert(timeout.milliseconds == 5000U);
    ++calls; memset(buffer, 0x5a, size); return A_STATUS_OK;
}
static aStatus_t storage_write(void *context, uint32_t address, const uint8_t *buffer,
                               uint32_t size, aTimeout_t timeout)
{
    (void)timeout;
    assert(context == &calls && address == 0x100000U && size == 4 && buffer);
    ++calls; return A_STATUS_OK;
}
static aStatus_t storage_erase(void *context, uint32_t address, uint32_t size,
                               aTimeout_t timeout)
{
    (void)timeout;
    assert(context == &calls && address == 0x100000U && size == 4096);
    ++calls; return A_STATUS_OK;
}
int main(void)
{
    aDataBaseStorage_t config = {
        .context = &calls, .capacity = 16U * 1024U * 1024U,
        .erase_block_size = 4096U, .read = storage_read,
        .write = storage_write, .erase = storage_erase,
    };
    assert(fal_partition_find("param") == NULL);
    assert(aDataBaseBindStorage(NULL) == A_STATUS_INVALID_PARAM);
    assert(aDataBaseBindStorage(&config) == A_STATUS_OK);
    assert(aDataBaseBindStorage(&config) == A_STATUS_BUSY);
    /* Descriptor is copied; the context remains borrowed. */
    config.read = NULL;
    const struct fal_partition *part = fal_partition_find("param");
    assert(part && fal_flash_device_find("flash25"));
    uint8_t buffer[4];
    assert(fal_partition_read(part, 0, buffer, sizeof(buffer)) == 0);
    assert(buffer[0] == 0x5a);
    assert(fal_partition_write(part, 0, buffer, sizeof(buffer)) == 0);
    assert(fal_partition_erase(part, 0, 4096) == 0);
    assert(fal_partition_read(part, part->len, buffer, 1) == -1);
    assert(calls == 3);
    assert(aDataBaseUnbindStorage() == A_STATUS_OK);
    assert(fal_partition_read(part, 0, buffer, 4) == -1);
    assert(aDataBaseUnbindStorage() == A_STATUS_NOT_READY);
    assert(aDataBaseBindStorage(&config) == A_STATUS_INVALID_PARAM);
    return 0;
}
