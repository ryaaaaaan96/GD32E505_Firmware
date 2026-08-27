#include "aDrv_usart.h"

bool aDrvUsartAsyncTxIsSupported(const aDrvUsartHandle_t *handle)
{
    (void)handle;
    return false;
}

aStatus_t aDrvUsartAsyncTxStart(aDrvUsartHandle_t *handle,
                                const void *data, size_t size,
                                size_t *started)
{
    (void)handle;
    (void)data;
    (void)size;
    (void)started;
    return A_STATUS_UNSUPPORTED;
}

aStatus_t aDrvUsartAsyncTxGetRemaining(aDrvUsartHandle_t *handle,
                                       size_t *remaining)
{
    (void)handle;
    (void)remaining;
    return A_STATUS_UNSUPPORTED;
}

aStatus_t aDrvUsartAsyncTxAbort(aDrvUsartHandle_t *handle)
{
    (void)handle;
    return A_STATUS_UNSUPPORTED;
}
