#include "status.h"
#include "app_system_device.h"
#include "aOS.h"

#define ASYSTEM_STATUS_BLINK_PERIOD_MS 500U

static void statusTask(void *argument)
{
    aDevLedHandle_t *led = argument;
    for (;;) {
        if (aDevLedToggle(led) != A_STATUS_OK) {
            (void)aDevLedOff(led);
            for (;;) {
                aOSDelayMs(ASYSTEM_STATUS_BLINK_PERIOD_MS);
            }
        }
        aOSDelayMs(ASYSTEM_STATUS_BLINK_PERIOD_MS);
    }
}

aStatus_t statusInit(void)
{
    aDevLedHandle_t *led = NULL;
    aStatus_t status = appSystemStatusLedInit(APP_LED_STATUS, &led);
    if (status != A_STATUS_OK) {
        return status;
    }
    return aOSCreateTask(statusTask, "status", 256U, led,
                         AOS_TASK_PRIO_NORMAL, NULL);
}
