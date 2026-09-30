#include "aBus.h"
#include <string.h>

static uint32_t first[3];
static uint32_t second[3];
static uint32_t mixed[3];

ABUS_STORAGE_EXPORT(first_binding, 0U, 1U, 0, first);
ABUS_STORAGE_EXPORT(second_binding, 0U, 1U, 1, second);
ABUS_STORAGE_EXPORT(mixed_binding, 0U, 2U, 0, mixed);

void bindings_fill(void)
{
    memset(first, 0xA5, sizeof(first));
    memset(second, 0xA5, sizeof(second));
    memset(mixed, 0xA5, sizeof(mixed));
}

int bindings_unchanged(void)
{
    const unsigned char *a = (const unsigned char *)first;
    const unsigned char *b = (const unsigned char *)second;
    const unsigned char *c = (const unsigned char *)mixed;

    for (size_t i = 0; i < sizeof(first); i++) {
        if (a[i] != 0xA5 || b[i] != 0xA5 || c[i] != 0xA5) return 0;
    }
    return 1;
}

void *bindings_first(void)
{
    return mixed;
}
