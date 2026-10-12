#ifndef ADRV_INTERNAL_H
#define ADRV_INTERNAL_H

#include "aDrv_gpio.h"
#include "gd32e50x.h"

#define ADRV_ARRAY_COUNT(array_) (sizeof(array_) / sizeof((array_)[0]))

typedef struct {
    uint32_t port;
    uint32_t pin_mask;
    rcu_periph_enum clock;
    uint8_t pin_number;
} aDrvPrivateGpio_t;

aStatus_t aDrvResolvePin(aDrvGpioPin_t pin, aDrvPrivateGpio_t *gpio);

/* DMA 单块的地址可达性；缓存约束由上层对完整保留区检查。
 * 硬件分块可跨越缓存行，不能将每块误当成独立的缓存所有权区域。 */
aStatus_t aDrvPrivateDmaMemoryCheck(const void *buffer, size_t size,
                                   aBool_t writable);

#endif
