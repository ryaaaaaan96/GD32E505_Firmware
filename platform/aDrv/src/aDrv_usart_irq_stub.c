#include "aDrv_usart.h"

aBool_t aDrvUsartInterruptIsSupported(void)
{
    return A_FALSE;
}

aStatus_t aDrvUsartRegisterCallback(
    aDrvUsartHandle_t *handle, const aDrvUsartExtiConfig_t *config)
{
    (void)handle;
    (void)config;
    return A_STATUS_UNSUPPORTED;
}

aStatus_t aDrvUsartUnregisterCallback(aDrvUsartHandle_t *handle,
                                      aDrvUsartExti_t trigger)
{
    (void)handle;
    (void)trigger;
    return A_STATUS_UNSUPPORTED;
}

aStatus_t aDrvUsartSetInterruptEnabled(aDrvUsartHandle_t *handle,
                                       aDrvUsartExti_t trigger,
                                       aBool_t enabled)
{
    (void)handle;
    (void)trigger;
    (void)enabled;
    return A_STATUS_UNSUPPORTED;
}

void aDrvUsartEnableInterrupt(aDrvUsartHandle_t *handle)
{
    (void)handle;
}

void aDrvUsartDisableInterrupt(aDrvUsartHandle_t *handle)
{
    (void)handle;
}
