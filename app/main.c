#include "aDrv.h"
#include "aOS.h"
#include "system.h"

static void appFatal(void)
{
    for (;;) {
    }
}

int main(void)
{
    if (aDrvInit() != A_STATUS_OK) {
        appFatal();
    }
    if (aOSInit() != A_STATUS_OK) {
        appFatal();
    }
    if (systemInit() != A_STATUS_OK) {
        appFatal();
    }

    aOSRun();
}
