#ifndef APP_LED_H
#define APP_LED_H
#include "aDev_led.h"

/* Application identity, not a hardware index. Keep values stable. */
typedef enum {
    APP_LED_STATUS = 0,
} appLedId_t;

/* Startup-only, single-threaded, after aDrv/aOS prerequisites; not ISR-safe.
 * Initialize the selected instance once and return its shared static handle.
 * Repeat calls return the same handle/saved error, never reconfigure or retry.
 * Reentrant initialization returns BUSY; this is not concurrent-call protection.
 * Failure clears output; NULL output: INVALID_PARAM; unknown/disabled ID: NOT_FOUND.
 * No allocation of a new instance, exclusive ownership, or Close. Borrowers must
 * not destroy/reinitialize the device. Initialization may allocate internal OS
 * resources and touch hardware; this is not a side-effect-free lookup.
 * Callers retain/pass the returned handle for runtime use.
 * See docs/interface_contract.md. */
aStatus_t appLedInit(appLedId_t id, aDevLedHandle_t **handle_out);
#endif
