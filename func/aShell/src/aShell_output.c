#include "aShell_internal.h"
#include "aShell_config.h"

#include <stdatomic.h>
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2,
               "Shell drop counter requires lock-free unsigned atomics");
_Static_assert(ASHELL_OUTPUT_BUFFER_SIZE >= ASHELL_PRINT_BUFFER_SIZE,
               "Output queue must hold a complete formatted message");

static struct {
    aOSMutex_t mutex;
    char data[ASHELL_OUTPUT_BUFFER_SIZE];
    size_t head;
    size_t tail;
    size_t used;
    atomic_uint dropped;
} output;

aStatus_t aShellOutputInit(void)
{
    output.head = 0U;
    output.tail = 0U;
    output.used = 0U;
    atomic_init(&output.dropped, 0U);
    return aOSMutexCreate(&output.mutex);
}

void aShellOutputDeInit(void)
{
    aOSMutexDestroy(&output.mutex);
    output.head = 0U;
    output.tail = 0U;
    output.used = 0U;
}

void aShellOutputDrop(void)
{
    (void)atomic_fetch_add_explicit(&output.dropped, 1U,
                                    memory_order_relaxed);
}

aStatus_t aShellOutputWrite(const char *data, size_t size)
{
    aStatus_t status;
    size_t first;

    if (size == 0U) return A_STATUS_OK;
    status = aOSMutexLock(output.mutex, A_TIMEOUT_NO_WAIT);
    if (status != A_STATUS_OK) {
        aShellOutputDrop();
        return status;
    }
    if (size > sizeof(output.data) - output.used) {
        (void)aOSMutexUnlock(output.mutex);
        aShellOutputDrop();
        return A_STATUS_BUSY;
    }
    first = sizeof(output.data) - output.head;
    if (first > size) first = size;
    memcpy(output.data + output.head, data, first);
    memcpy(output.data, data + first, size - first);
    output.head = (output.head + size) % sizeof(output.data);
    output.used += size;
    (void)aOSMutexUnlock(output.mutex);
    return A_STATUS_OK;
}

/* Exactly one Process caller consumes. Bytes remain occupied while write is
 * running, so producers cannot overwrite the span passed to the stream.
 * Drain only the initial snapshot to bound work under continuous producers.
 */
aStatus_t aShellOutputDrain(void)
{
    aTimepoint_t deadline;
    aStatus_t status;
    aSSize_t count;
    size_t remaining;
    size_t size;
    size_t tail;

    status = aOSMutexLock(output.mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    remaining = output.used;
    tail = output.tail;
    (void)aOSMutexUnlock(output.mutex);
    deadline = aTimepointCalc(
        aShellContext.config.write_timeout, aOSGetUptimeMs());
    while (remaining != 0U) {
        size = sizeof(output.data) - tail;
        if (size > remaining) size = remaining;
        count = aShellContext.config.stream.write(
            output.data + tail, size,
            aTimepointRemaining(&deadline, aOSGetUptimeMs()));
        if (count < -1 || count > (aSSize_t)size) return A_STATUS_ERROR;
        if (count < 0) return aShellIoError();
        if (count == 0) return A_STATUS_BUSY;

        status = aOSMutexLock(output.mutex, A_TIMEOUT_FOREVER);
        if (status != A_STATUS_OK) return status;
        tail = (tail + (size_t)count) % sizeof(output.data);
        output.tail = tail;
        output.used -= (size_t)count;
        (void)aOSMutexUnlock(output.mutex);
        remaining -= (size_t)count;
        if (remaining != 0U &&
            aTimepointExpired(&deadline, aOSGetUptimeMs())) {
            return A_STATUS_TIMEOUT;
        }
    }
    return A_STATUS_OK;
}

aStatus_t aShellGetOutputStats(aShellOutputStats_t *stats)
{
    aStatus_t status;

    if (stats == NULL) return A_STATUS_INVALID_PARAM;
    if (!aShellContext.ready) return A_STATUS_NOT_READY;
    status = aOSMutexLock(output.mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    stats->pending_bytes = output.used;
    stats->dropped_messages = atomic_load_explicit(
        &output.dropped, memory_order_relaxed);
    (void)aOSMutexUnlock(output.mutex);
    return A_STATUS_OK;
}
