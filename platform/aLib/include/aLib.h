#ifndef A_LIB_H
#define A_LIB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define ALIB_USED __attribute__((used))
#define ALIB_WEAK __attribute__((weak))
#define ALIB_SECTION(name_) __attribute__((section(name_), used))
#define ALIB_NORETURN __attribute__((noreturn))
#else
#define ALIB_USED
#define ALIB_WEAK
#define ALIB_SECTION(name_)
#define ALIB_NORETURN
#endif

typedef ptrdiff_t aSSize_t;

typedef enum {
    A_ERRNO_NONE = 0,
    A_EINVAL,
    A_EAGAIN,
    A_ETIMEDOUT,
    A_EIO,
    A_ENODEV,
    A_ENOTSUP,
    A_EINTR,
} aErrno_t;

typedef enum {
    A_TIMEOUT_TYPE_RELATIVE = 0,
    A_TIMEOUT_TYPE_FOREVER,
} aTimeoutType_t;

typedef struct {
    uint32_t milliseconds;
    aTimeoutType_t type;
} aTimeout_t;

typedef struct {
    uint32_t start_ms;
    uint32_t duration_ms;
    bool forever;
} aTimepoint_t;

#define A_TIMEOUT_NO_WAIT \
    ((aTimeout_t){.milliseconds = 0U, .type = A_TIMEOUT_TYPE_RELATIVE})

#define A_TIMEOUT_MS(value_) \
    ((aTimeout_t){ \
        .milliseconds = (uint32_t)(value_), \
        .type = A_TIMEOUT_TYPE_RELATIVE, \
    })

#define A_TIMEOUT_FOREVER \
    ((aTimeout_t){.milliseconds = 0U, .type = A_TIMEOUT_TYPE_FOREVER})

static inline bool aTimeoutIsValid(aTimeout_t timeout)
{
    return (timeout.type == A_TIMEOUT_TYPE_RELATIVE) ||
           (timeout.type == A_TIMEOUT_TYPE_FOREVER);
}

static inline aTimepoint_t aTimepointCalc(aTimeout_t timeout,
                                          uint32_t now_ms)
{
    const aTimepoint_t timepoint = {
        .start_ms = now_ms,
        .duration_ms = timeout.milliseconds,
        .forever = timeout.type == A_TIMEOUT_TYPE_FOREVER,
    };

    return timepoint;
}

static inline bool aTimepointExpired(const aTimepoint_t *timepoint,
                                     uint32_t now_ms)
{
    return (timepoint != NULL) && !timepoint->forever &&
           ((uint32_t)(now_ms - timepoint->start_ms) >=
            timepoint->duration_ms);
}

static inline aTimeout_t aTimepointRemaining(const aTimepoint_t *timepoint,
                                              uint32_t now_ms)
{
    uint32_t elapsed_ms;

    if ((timepoint == NULL) || timepoint->forever) {
        return A_TIMEOUT_FOREVER;
    }

    elapsed_ms = (uint32_t)(now_ms - timepoint->start_ms);
    if (elapsed_ms >= timepoint->duration_ms) {
        return A_TIMEOUT_NO_WAIT;
    }

    return A_TIMEOUT_MS(timepoint->duration_ms - elapsed_ms);
}

#endif
