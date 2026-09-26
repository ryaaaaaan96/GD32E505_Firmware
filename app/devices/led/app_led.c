#include "aclass_system_config.h"
#include "app_led.h"

static const aDevLedConfig_t config = {
    .pin = ASYSTEM_STATUS_LED_PIN,
    .active_level = ASYSTEM_STATUS_LED_ACTIVE_LEVEL,
    .initially_on = A_FALSE,
};

static aDevLedHandle_t handle;
enum { INSTANCE_COLD, INSTANCE_STARTING, INSTANCE_DONE };
static unsigned phase;
static aStatus_t init_result = A_STATUS_NOT_READY;

aStatus_t appLedInit(appLedId_t id, aDevLedHandle_t **handle_out)
{
    if (handle_out == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    *handle_out = NULL;
    switch (id) {
    case APP_LED_STATUS:
        if (phase == INSTANCE_STARTING) {
            return A_STATUS_BUSY;
        }
        if (phase == INSTANCE_COLD) {
            phase = INSTANCE_STARTING;
            init_result = aDevLedInit(&config, &handle);
            phase = INSTANCE_DONE;
        }
        if (init_result != A_STATUS_OK) {
            return init_result;
        }
        *handle_out = &handle;
        return A_STATUS_OK;
    default:
        return A_STATUS_NOT_FOUND;
    }
}
