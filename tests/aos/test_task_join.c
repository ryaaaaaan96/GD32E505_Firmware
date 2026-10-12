/* FreeRTOS 只保留 Join 接口，不等待、不访问或修改任务句柄。 */
#include <assert.h>
#include <stdio.h>
#include "../../platform/aOS/backend/freertos/aOS_freertos.c"

int main(void)
{
    int token;
    aOSTaskHandle_t handle = &token;
    const aTimeout_t timeouts[] = {
        A_TIMEOUT_NO_WAIT, A_TIMEOUT_MS(10U), A_TIMEOUT_FOREVER
    };

    for (size_t i = 0U; i < sizeof(timeouts) / sizeof(timeouts[0]); ++i) {
        assert(aOSTaskJoin(&handle, timeouts[i]) == A_STATUS_UNSUPPORTED);
        assert(handle == &token);
    }
    assert(aOSTaskJoin(NULL, A_TIMEOUT_NO_WAIT) == A_STATUS_UNSUPPORTED);
    handle = NULL;
    assert(aOSTaskJoin(&handle, A_TIMEOUT_NO_WAIT) == A_STATUS_UNSUPPORTED);
    assert(handle == NULL);
    puts("aOS FreeRTOS Join unsupported semantics passed");
    return 0;
}
