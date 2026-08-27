#ifndef ADRV_BASIC_H
#define ADRV_BASIC_H

#include "aDrv.h"

typedef uint16_t aDrvGpioPin_t;
typedef uint8_t aDrvDmaChannel_t;

#define ADRV_PIN(port_, pin_)                                                 \
    ((aDrvGpioPin_t)(((uint16_t)(port_) * 16U) + (uint16_t)(pin_)))
#define ADRV_PIN_NONE         ((aDrvGpioPin_t)0xFFFFU)
#define ADRV_PINNULL          ADRV_PIN_NONE
#define ADRV_DMA_CHANNEL_NONE ((aDrvDmaChannel_t)0xFFU)

typedef enum {
    ADRV_GPIO_PORT_A,
    ADRV_GPIO_PORT_B,
    ADRV_GPIO_PORT_C,
    ADRV_GPIO_PORT_D,
    ADRV_GPIO_PORT_E,
    ADRV_GPIO_PORT_F,
    ADRV_GPIO_PORT_G,
} aDrvGpioPort_t;

uint32_t aDrvGetChipId(void);
uint32_t aDrvGetRevisionId(void);
uint32_t aDrvGetCoreClockHz(void);
uint16_t aDrvGetFlashSize(void);
aStatus_t aDrvGetUniqueId(uint8_t uid[12]);

#endif
