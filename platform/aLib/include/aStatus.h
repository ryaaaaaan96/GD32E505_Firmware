#ifndef A_STATUS_H
#define A_STATUS_H

/* Shared status for fallible non-stream operations. Do not duplicate this enum
 * per module. Output data is returned through typed output parameters.
 * Public API design rules: docs/interface_contract.md. */
typedef enum {
    A_STATUS_OK = 0,
    A_STATUS_ERROR = -1,
    A_STATUS_INVALID_PARAM = -2,
    A_STATUS_TIMEOUT = -3,
    A_STATUS_BUSY = -4,
    A_STATUS_UNSUPPORTED = -5,
    A_STATUS_NOT_READY = -6,
    A_STATUS_NO_MEMORY = -7,
    A_STATUS_CANCELLED = -8,
    A_STATUS_NOT_FOUND = -9
} aStatus_t;

#endif
