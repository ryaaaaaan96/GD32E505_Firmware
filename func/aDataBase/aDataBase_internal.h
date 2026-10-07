#ifndef ADATABASE_INTERNAL_H
#define ADATABASE_INTERNAL_H

#include "aDataBase_instance.h"
#include "aOS.h"

aStatus_t aDataBaseOperationBegin(aTimeout_t timeout);
aStatus_t aDataBaseOperationEnd(aStatus_t status);
aStatus_t aDataBaseStorageError(void);
void aDataBaseStorageFail(aStatus_t status);
void aDataBaseInstanceAdd(aDataBaseInstance_t *instance);
void aDataBaseInstanceRemove(aDataBaseInstance_t *instance);
aBool_t aDataBasePartitionIsUsed(const struct fal_partition *partition);

#endif
