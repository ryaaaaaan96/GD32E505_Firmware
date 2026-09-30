#ifndef ADEV_FLASH25Q_INSTANCE_H
#define ADEV_FLASH25Q_INSTANCE_H

#include "aDev_flash25q.h"
#include "sfud.h"

/* Static allocation only. Layout is private, not a stable ABI.
 * SFUD configuration must match the library. Never call SFUD directly. */
struct aDevFlash25qBus {
    aDrvSpiHandle_t spi;
    void *mutex;
    size_t references;
    aBool_t fault;
};

struct aDevFlash25qHandle {
    sfud_flash flash;
    aDevFlash25qBus_t *bus;
    aDrvGpioHandle_t cs;
    aTimepoint_t deadline;
    aStatus_t port_error;
    aBool_t dynamic;
};

#endif
