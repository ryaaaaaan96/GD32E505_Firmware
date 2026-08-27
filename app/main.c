#include "aDrv.h"
#include "aOS.h"
#include "aclass_system.h"

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
    if (aSystemInit() != A_STATUS_OK) {
        appFatal();
    }

    aOSRun();
}
