/* 与固件相同的通知合并语义；超时推进测试时钟，不进行真实休眠。 */
#include "aOS.h"
#include <stdatomic.h>

static aBool_t fail_create;
void testFailWaitCreate(aBool_t fail) { fail_create = fail; }

aStatus_t aOSWaitObjectCreate(aOSWaitObject_t *out)
{
    atomic_bool *pending;
    if (fail_create) return A_STATUS_NO_MEMORY;
    pending = aOSAlloc(sizeof(*pending));
    if (pending == NULL) return A_STATUS_NO_MEMORY;
    atomic_init(pending, A_FALSE);
    *out = pending;
    return A_STATUS_OK;
}

void aOSWaitObjectDestroy(aOSWaitObject_t *object)
{
    aOSFree(*object);
    *object = NULL;
}

void aOSWaitObjectNotify(aOSWaitObject_t object)
{
    atomic_store((atomic_bool *)object, A_TRUE);
}

aStatus_t aOSWaitObjectWait(aOSWaitObject_t object, aTimeout_t timeout)
{
    if (atomic_exchange((atomic_bool *)object, A_FALSE)) return A_STATUS_OK;
    if (timeout.type == A_TIMEOUT_TYPE_FOREVER) return A_STATUS_NOT_READY;
    if (timeout.milliseconds == 0U) return A_STATUS_BUSY;
    aOSDelayMs(timeout.milliseconds);
    return A_STATUS_TIMEOUT;
}
