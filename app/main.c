#include "aDrv.h"
#include "aOS.h"
#include "system.h"

static void appFatal(aOSFaultCode_t code, aStatus_t status,
                     const char *context)
{
    aOSRecordFault(code, status, context);
    for (;;) {
    }
}

int main(void)
{
    aStatus_t status = aDrvInit();
    if (status != A_STATUS_OK) {
        appFatal(AOS_FAULT_APP_INIT, status, "aDrvInit");
    }
    status = aOSInit();
    if (status != A_STATUS_OK) {
        appFatal(AOS_FAULT_APP_INIT, status, "aOSInit");
    }
    status = aSystemInit();
    if (status != A_STATUS_OK) {
        appFatal(AOS_FAULT_APP_INIT, status, "aSystemInit");
    }

    aOSRun();
    appFatal(AOS_FAULT_SCHEDULER_RETURNED, A_STATUS_ERROR,
             "aOSRun returned");
}
