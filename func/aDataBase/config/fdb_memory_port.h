#ifndef FDB_MEMORY_PORT_H
#define FDB_MEMORY_PORT_H

#include "aMemory.h"

/* FlashDB 存储补丁使用 aMemory 直接读写；以下仅提供当前数据库操作的
 * 剩余时间和首错记录，调用方必须已持有数据库模块锁。 */
aStatus_t aDataBaseStorageError(void);
void aDataBaseStorageFail(aStatus_t status);
aTimeout_t aDataBaseStorageTimeout(void);

#endif
