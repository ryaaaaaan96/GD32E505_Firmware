#include "aBus_table.h"

#include <stdio.h>
#include <stdlib.h>

/* 不使用 assert，确保 NDEBUG 下仍检查真实返回值。 */
#define CHECK(expression_) do { \
    if (!(expression_)) { \
        fprintf(stderr, "line %d: %s\n", __LINE__, #expression_); \
        abort(); \
    } \
} while (0)

typedef struct {
    uint16_t mode;
    uint16_t reserved;
    uint32_t speed;
} Example_t;

ALIB_ASSERT_TYPE_SIZE(Example_t, 8U);

int main(void)
{
    aBusParam_t params[] = {
        {.offset = offsetof(Example_t, mode), .size = sizeof(uint16_t),
         .type = ALIB_DATA_U16},
        {.offset = offsetof(Example_t, reserved), .size = sizeof(uint16_t),
         .type = ALIB_DATA_U16},
        {.offset = offsetof(Example_t, speed), .size = sizeof(uint32_t),
         .type = ALIB_DATA_U32},
    };
    const aBusSig_t valid = {
        .type = ALIB_DATA_STRUCT, .size = sizeof(Example_t),
        .params = params, .param_count = sizeof(params) / sizeof(params[0])
    };
    aBusSig_t sig = valid;

    CHECK(aBusSigParamSizeCheck(&valid) == A_STATUS_OK);
    CHECK(aBusSigParamSizeCheck(NULL) == A_STATUS_INVALID_PARAM);
    sig.type = ALIB_DATA_RAW;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_UNSUPPORTED);
    sig = valid;
    sig.params = NULL;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_INVALID_PARAM);
    sig = valid;
    sig.param_count = 0U;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_INVALID_PARAM);
    sig = valid;
    sig.size = 0U;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_INVALID_PARAM);
    sig.size = sizeof(Example_t) + 1U;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_INVALID_PARAM);
    sig.size = sizeof(Example_t) - 1U;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_INVALID_PARAM);
    sig = valid;
    params[0].size = 0U;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_INVALID_PARAM);
    params[0].size = sizeof(uint16_t);

    /* 重复字段掩盖遗漏时总长度仍相等；此工具不声称检查完整布局。 */
    params[1].offset = params[0].offset;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_OK);

    /* 两个奇数字节字段也允许，寄存器约束由协议层检查。 */
    params[0].size = 1U;
    params[0].type = ALIB_DATA_U8;
    params[1].offset = 1U;
    params[1].size = 1U;
    params[1].type = ALIB_DATA_U8;
    sig.size = 2U;
    sig.param_count = 2U;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_OK);

    /* 如果直接累加，会回绕到 2 并误判成功。 */
    params[0].size = SIZE_MAX;
    params[1].size = 3U;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_INVALID_PARAM);
    sig.size = SIZE_MAX;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_INVALID_PARAM);
    sig.param_count = 1U;
    CHECK(aBusSigParamSizeCheck(&sig) == A_STATUS_OK);
    return 0;
}
