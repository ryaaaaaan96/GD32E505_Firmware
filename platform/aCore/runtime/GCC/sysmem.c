#include <errno.h>
#include <stddef.h>
#include <stdint.h>

static uintptr_t s_heap_end;

void *_sbrk(ptrdiff_t increment)
{
    extern uint8_t _heap_start;
    extern uint8_t _heap_end;
    const uintptr_t heap_start = (uintptr_t)&_heap_start;
    const uintptr_t heap_limit = (uintptr_t)&_heap_end;
    uintptr_t next_heap_end;
    uintptr_t previous_heap_end;

    if (s_heap_end == 0U) {
        s_heap_end = heap_start;
    }

    previous_heap_end = s_heap_end;
    if (increment >= 0) {
        const uintptr_t growth = (uintptr_t)increment;

        if ((s_heap_end > heap_limit) ||
            (growth > (heap_limit - s_heap_end))) {
            errno = ENOMEM;
            return (void *)-1;
        }
        next_heap_end = s_heap_end + growth;
    } else {
        const uintptr_t shrink = (uintptr_t)(-(increment + 1)) + 1U;

        if ((s_heap_end < heap_start) ||
            (shrink > (s_heap_end - heap_start))) {
            errno = EINVAL;
            return (void *)-1;
        }
        next_heap_end = s_heap_end - shrink;
    }

    s_heap_end = next_heap_end;
    return (void *)previous_heap_end;
}
