#include "aDrv_basic.h"

#include "gd32e50x.h"

#define GD32_FLASH_SIZE_ADDRESS 0x1FFFF7E0UL
#define GD32_UNIQUE_ID_ADDRESS  0x1FFFF7E8UL
#define GD32_UNIQUE_ID_SIZE     12U

uint32_t aDrvGetChipId(void)
{
    return DBG_ID;
}

uint32_t aDrvGetRevisionId(void)
{
    return DBG_ID;
}

uint16_t aDrvGetFlashSize(void)
{
    return *(const volatile uint16_t *)GD32_FLASH_SIZE_ADDRESS;
}

aStatus_t aDrvGetUniqueId(uint8_t uid[GD32_UNIQUE_ID_SIZE])
{
    const volatile uint8_t *source;

    if (uid == NULL) {
        return A_STATUS_INVALID_PARAM;
    }

    source = (const volatile uint8_t *)GD32_UNIQUE_ID_ADDRESS;
    for (uint32_t index = 0U; index < GD32_UNIQUE_ID_SIZE; ++index) {
        uid[index] = source[index];
    }

    return A_STATUS_OK;
}
