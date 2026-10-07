#include "aDataBase_internal.h"
#include "aMemory_layout.h"
#include "fdb_low_lvl.h"
#include <assert.h>
#include <string.h>

extern size_t database_allocations;
static uint8_t memory[AMEMORY_FLASH_CAPACITY_BYTES];
static unsigned calls;

static aStatus_t read_bytes(
    void *context, const aMemoryReadRequest_t *request)
{
    assert(context == memory);
    assert(request->address == AMEMORY_PART_PARAM_OFFSET);
    assert(request->timeout.milliseconds == 5000U);
    ++calls;
    memset(request->data, 0x5A, request->size);
    return A_STATUS_OK;
}
static aStatus_t write_bytes(
    void *context, const aMemoryWriteRequest_t *request)
{
    assert(context == memory);
    assert(request->address == AMEMORY_PART_PARAM_OFFSET);
    assert(request->size == 4U && request->data != NULL);
    ++calls;
    return A_STATUS_OK;
}
static aStatus_t erase_bytes(
    void *context, const aMemoryEraseRequest_t *request)
{
    assert(context == memory);
    assert(request->address == AMEMORY_PART_PARAM_OFFSET);
    assert(request->size == 4096U);
    ++calls;
    return A_STATUS_OK;
}
#include "memory_fixture.h"

int main(void)
{
    struct fdb_db db;
    uint8_t buffer[4];

    memset(&db, 0, sizeof(db));
    assert(aDataBaseInit() == A_STATUS_NOT_READY);
    assert(memory_start() == A_STATUS_OK);
    assert(aDataBaseInit() == A_STATUS_OK);
    assert(aDataBaseInit() == A_STATUS_BUSY);
    assert(aMemoryDeInit() == A_STATUS_BUSY);
    db.storage.memory = aMemoryFind(AMEMORY_PART_PARAM_NAME);
    assert(db.storage.memory != NULL);
    /* 未进入模块事务时，官方存储入口不能访问设备。 */
    assert(_fdb_flash_read(&db, 0U, buffer, 4U) == FDB_READ_ERR);
    assert(calls == 0U);
    assert(aDataBaseOperationBegin(A_TIMEOUT_MS(5000U)) == A_STATUS_OK);
    assert(_fdb_flash_read(&db, 0U, buffer, 4U) == FDB_NO_ERR);
    assert(buffer[0] == 0x5A);
    assert(_fdb_flash_write(&db, 0U, buffer, 4U, true) == FDB_NO_ERR);
    assert(_fdb_flash_erase(&db, 0U, 4096U) == FDB_NO_ERR);
    assert(_fdb_flash_read(&db, AMEMORY_PART_PARAM_SIZE, buffer, 1U) ==
           FDB_READ_ERR);
    /* 首错阻止后续访问，并由事务出口还原具体状态码。 */
    assert(_fdb_flash_write(&db, 0U, buffer, 4U, true) == FDB_WRITE_ERR);
    assert(aDataBaseOperationEnd(A_STATUS_OK) == A_STATUS_INVALID_PARAM);
    assert(calls == 3U);
    assert(aDataBaseDeInit() == A_STATUS_OK);
    assert(aDataBaseDeInit() == A_STATUS_NOT_READY);
    assert(aMemoryDeInit() == A_STATUS_OK);
    assert(database_allocations == 0U);
    return 0;
}
