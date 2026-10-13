/** @brief 通用数据范围与字段布局，供数据容器和通信端点共享。 */
#ifndef ALIB_DATA_LAYOUT_H
#define ALIB_DATA_LAYOUT_H
#include "aDataType.h"
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
#endif
