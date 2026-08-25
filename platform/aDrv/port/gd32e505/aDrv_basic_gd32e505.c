#include "aDrv_basic.h"
#include "gd32e50x.h"
aStatus_t aDrvParseGpio(aDrvGpioPin_t pin, aDrvGpioInfo_t *info)
{
    if ((info == NULL) || !aDrvIsGpioValid(pin)) return A_STATUS_INVALID_PARAM;
    static const uintptr_t ports[] = { GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG };
    info->port = ports[pin / 16U]; info->pin_number = (uint8_t)(pin % 16U);
    info->pin_mask = 1UL << info->pin_number; return A_STATUS_OK;
}
int32_t aDrvIsGpioValid(aDrvGpioPin_t pin) { return pin < ADRV_PIN(ADRV_GPIO_PORT_COUNT, 0U); }
aStatus_t aDrvParseDma(aDrvDmaChannel_t channel, aDrvDmaInfo_t *info)
{
    if ((info == NULL) || !aDrvIsDmaValid(channel)) return A_STATUS_INVALID_PARAM;
    info->controller = channel < 7U ? DMA0 : DMA1; info->channel = channel < 7U ? channel : (uint8_t)(channel - 7U);
    return A_STATUS_OK;
}
int32_t aDrvIsDmaValid(aDrvDmaChannel_t channel) { return channel < 14U; }
uint32_t aDrvGetChipId(void) { return DBG_ID; }
uint32_t aDrvGetRevisionId(void) { return DBG_ID; }
uint16_t aDrvGetFlashSize(void) { return *(const volatile uint16_t *)0x1FFFF7E0UL; }
aStatus_t aDrvGetUniqueId(uint8_t uid[12])
{
    if (uid == NULL) return A_STATUS_INVALID_PARAM;
    const volatile uint8_t *source = (const volatile uint8_t *)0x1FFFF7E8UL;
    for (uint32_t i = 0U; i < 12U; ++i) uid[i] = source[i];
    return A_STATUS_OK;
}
