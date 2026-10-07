#ifndef ADEV_FLASH25Q_INSTANCE_H
#define ADEV_FLASH25Q_INSTANCE_H

#include "aDev_flash25q.h"
#include "sfud.h"

/* 供静态分配对象存储使用；内部布局不构成稳定的二进制接口。
 * SFUD 配置必须与库编译时一致；应用不得直接调用 SFUD。 */
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
