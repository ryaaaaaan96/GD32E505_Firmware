#ifndef ADATABASE_INTERNAL_H
#define ADATABASE_INTERNAL_H

#include "aDataBase_instance.h"
#include "aOS.h"

aStatus_t aDataBaseOperationBegin(aTimeout_t timeout);
aStatus_t aDataBaseOperationEnd(aStatus_t status);
aStatus_t aDataBaseStorageError(void);
void aDataBaseStorageFail(aStatus_t status);
aTimeout_t aDataBaseStorageTimeout(void);
void aDataBaseInstanceAdd(aDataBaseInstance_t *instance);
void aDataBaseInstanceRemove(aDataBaseInstance_t *instance);
aBool_t aDataBasePartitionIsUsed(const aMemoryHandle_t *partition);

aStatus_t aDataBaseIndexPrepare(aDataBaseKvHandle_t *handle,
    const aDataBaseKvConfig_t *config, aBool_t dynamic);
aStatus_t aDataBaseIndexBuild(aDataBaseKvHandle_t *handle);
void aDataBaseIndexRelease(aDataBaseKvHandle_t *handle);
aStatus_t aDataBaseIndexSelect(aDataBaseKvHandle_t *handle,
    uint16_t deviceID, size_t sigIndex, const aBusSig_t **sig);

#endif
