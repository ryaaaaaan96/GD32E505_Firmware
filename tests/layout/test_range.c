#include "aDataLayout.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const aDataRange_t u8 = {.min.u8 = 1U, .max.u8 = UINT8_MAX};
    const aDataRange_t u16 = {.min.u16 = 2U, .max.u16 = UINT16_MAX};
    const aDataRange_t u32 = {.min.u32 = 1U, .max.u32 = UINT32_MAX};
    const aDataRange_t s32 = {.min.s32 = -10, .max.s32 = 10};
    uint8_t unaligned[9];
    uint32_t number = UINT32_MAX;
    int32_t signed_number = -10;
    assert(aDataRangeCheck(ALIB_DATA_RAW, NULL, NULL));
    assert(!aDataRangeCheck(ALIB_DATA_RAW, &u32, unaligned));
    assert(!aDataRangeCheck((aDataType_t)99, &u32, unaligned));
    assert(!aDataRangeCheck(ALIB_DATA_U8, &u8, NULL));
    assert(!aDataRangeCheck(ALIB_DATA_U16, &u16, NULL));
    assert(!aDataRangeCheck(ALIB_DATA_U32, &u32, NULL));
    assert(aDataRangeCheck(ALIB_DATA_S32, &s32, NULL));
    memcpy(unaligned + 1, &number, sizeof(number));
    assert(aDataRangeCheck(ALIB_DATA_U32, &u32, unaligned + 1));
    memcpy(unaligned + 1, &signed_number, sizeof(signed_number));
    assert(aDataRangeCheck(ALIB_DATA_S32, &s32, unaligned + 1));
    signed_number--;
    memcpy(unaligned + 1, &signed_number, sizeof(signed_number));
    assert(!aDataRangeCheck(ALIB_DATA_S32, &s32, unaligned + 1));
    const aDataRange_t reversed = {.min.u32 = 20U, .max.u32 = 10U};
    assert(!aDataValueInRange(ALIB_DATA_U32, &reversed, reversed.min));
    puts("Scalar ranges: unaligned, zero defaults, limits and invalid types passed");
    return 0;
}
