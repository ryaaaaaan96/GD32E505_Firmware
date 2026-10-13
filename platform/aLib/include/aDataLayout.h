/** @brief 通用数据范围与字段布局，供数据容器和通信端点共享。 */
#ifndef ALIB_DATA_LAYOUT_H
#define ALIB_DATA_LAYOUT_H
#include "aDataType.h"
#include "aLib.h"
#include <string.h>
typedef struct {
    aDataValue_t min;
    aDataValue_t max;
} aDataRange_t;
typedef struct {
    size_t offset;
    size_t size;
    aDataType_t type;
    const aDataRange_t *range;
} aDataField_t;

/** 无范围时直接通过；有范围时只接受定长整数，不做类型转换。 */
static inline aBool_t aDataValueInRange(aDataType_t type,
    const aDataRange_t *range, aDataValue_t value)
{
    if (range == NULL) return A_TRUE;
    switch (type) {
    case ALIB_DATA_U8:
        return value.u8 >= range->min.u8 && value.u8 <= range->max.u8;
    case ALIB_DATA_U16:
        return value.u16 >= range->min.u16 && value.u16 <= range->max.u16;
    case ALIB_DATA_U32:
        return value.u32 >= range->min.u32 && value.u32 <= range->max.u32;
    case ALIB_DATA_S32:
        return value.s32 >= range->min.s32 && value.s32 <= range->max.s32;
    default: return A_FALSE;
    }
}

/** 支持非对齐数据；data 为 NULL 时检查零默认值。范围端点均包含。 */
static inline aBool_t aDataRangeCheck(aDataType_t type,
    const aDataRange_t *range, const void *data)
{
    aDataValue_t value;
    if (range == NULL) return A_TRUE;
    memset(&value, 0, sizeof(value));
    const size_t size = aDataTypeSize(type);
    if (size == 0U) return A_FALSE;
    /* 按成员复制，兼容大小端主机，且不要求源地址按整数对齐。 */
    void *destination;
    switch (type) {
    case ALIB_DATA_U8: destination = &value.u8; break;
    case ALIB_DATA_U16: destination = &value.u16; break;
    case ALIB_DATA_U32: destination = &value.u32; break;
    case ALIB_DATA_S32: destination = &value.s32; break;
    default: return A_FALSE;
    }
    if (data != NULL) memcpy(destination, data, size);
    return aDataValueInRange(type, range, value);
}
#endif
