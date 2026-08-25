#ifndef A_STATUS_H
#define A_STATUS_H

typedef enum {
    A_STATUS_OK = 0,
    A_STATUS_ERROR = -1,
    A_STATUS_INVALID_PARAM = -2,
    A_STATUS_TIMEOUT = -3,
    A_STATUS_BUSY = -4,
    A_STATUS_UNSUPPORTED = -5,
    A_STATUS_NOT_READY = -6,
    A_STATUS_NO_MEMORY = -7
} aStatus_t;

#endif
