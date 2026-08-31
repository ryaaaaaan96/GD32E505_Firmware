#include "aDrv_usart.h"

aBool_t aDrvUsartAsyncTxIsSupported(const aDrvUsartHandle_t *handle)
{
    (void)handle;
    return A_FALSE;
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

aBool_t aDrvUsartAsyncRxIsSupported(const aDrvUsartHandle_t *handle)
{
    (void)handle;
    return A_FALSE;
}

aStatus_t aDrvUsartAsyncRxStart(aDrvUsartHandle_t *handle,
                                void *buffer, size_t size)
{
    (void)handle;
    (void)buffer;
    (void)size;
    return A_STATUS_UNSUPPORTED;
}

aStatus_t aDrvUsartAsyncRxCircularStart(aDrvUsartHandle_t *handle,
                                        void *buffer, size_t size,
                                        uint8_t interrupt_priority)
{
    (void)handle;
    (void)buffer;
    (void)size;
    (void)interrupt_priority;
    return A_STATUS_UNSUPPORTED;
}

aStatus_t aDrvUsartAsyncRxGetReceivedCount(aDrvUsartHandle_t *handle,
                                           size_t *received)
{
    (void)handle;
    (void)received;
    return A_STATUS_UNSUPPORTED;
}

aStatus_t aDrvUsartAsyncRxStop(aDrvUsartHandle_t *handle,
                               size_t *received)
{
    (void)handle;
    (void)received;
    return A_STATUS_UNSUPPORTED;
}

aStatus_t aDrvUsartAsyncRxAbort(aDrvUsartHandle_t *handle)
{
    (void)handle;
    return A_STATUS_UNSUPPORTED;
}
