/* 验证实际 FreeRTOS 适配层统一记录错误，成功释放不会清除旧故障。 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../platform/aOS/backend/freertos/aOS_freertos.c"

static unsigned gives;
static int mutex;
static BaseType_t give_result = pdTRUE;

BaseType_t xSemaphoreGive(SemaphoreHandle_t handle)
{
    assert(handle == &mutex);
    gives++;
    return give_result;
}

int main(void)
{
    aOSRecordFault(AOS_FAULT_APP_INIT, A_STATUS_ERROR, "previous");
    assert(aOSMutexUnlock(&mutex) == A_STATUS_OK);
    assert(gives == 1U);
    assert(g_aOSFaultRecord.code == AOS_FAULT_APP_INIT);
    assert(strcmp(g_aOSFaultRecord.context, "previous") == 0);

    give_result = pdFALSE;
    assert(aOSMutexUnlock(&mutex) == A_STATUS_ERROR);
    assert(gives == 2U);
    assert(g_aOSFaultRecord.code == AOS_FAULT_MUTEX_UNLOCK);
    assert(g_aOSFaultRecord.status == A_STATUS_ERROR);
    assert(strcmp(g_aOSFaultRecord.context, "aOSMutexUnlock") == 0);

    /* 空句柄不传给内核，也需要保留调用错误。 */
    assert(aOSMutexUnlock(NULL) == A_STATUS_INVALID_PARAM);
    assert(gives == 2U);
    assert(g_aOSFaultRecord.code == AOS_FAULT_MUTEX_UNLOCK);
    assert(g_aOSFaultRecord.status == A_STATUS_INVALID_PARAM);

    give_result = pdTRUE;
    assert(aOSMutexUnlock(&mutex) == A_STATUS_OK);
    assert(gives == 3U);
    assert(g_aOSFaultRecord.status == A_STATUS_INVALID_PARAM);
    puts("aOS mutex unlock status/fault recording passed");
    return 0;
}
