/* 通用命令仅从这些测试点表查询；不链接任何产品点表。 */
#include "sig_data.h"
#include "aBus_instance.h"
#include "aOS.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char output[4096];
static size_t output_used;
static int allocation_fail;
static int allocations;

int test_print(const char *format, ...)
{
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(output + output_used, sizeof(output) - output_used,
                       format, args);
    va_end(args);
    assert(length >= 0 && (size_t)length < sizeof(output) - output_used);
    output_used += (size_t)length;
    return length;
}

void *aOSAlloc(size_t size)
{
    void *data;

    if (allocation_fail) return NULL;
    data = malloc(size);
    if (data != NULL) allocations++;
    return data;
}

void aOSFree(void *data)
{
    if (data != NULL) allocations--;
    free(data);
}

static uint8_t u8;
static uint16_t u16;
static uint32_t u32;
static int32_t s32;
static unsigned char raw[3];
static unsigned char group[4];
ABUS_RAM_BIND_EXPORT(t_u8, 0U, 90U, 0U, u8);
ABUS_RAM_BIND_EXPORT(t_u16, 0U, 90U, 1U, u16);
ABUS_RAM_BIND_EXPORT(t_u32, 0U, 90U, 2U, u32);
ABUS_RAM_BIND_EXPORT(t_s32, 0U, 90U, 3U, s32);
ABUS_RAM_BIND_EXPORT(t_raw, 0U, 90U, 4U, raw);
ABUS_RAM_BIND_EXPORT(t_group, 0U, 90U, 5U, group);
static const aBusRange_t range = {.max.u8 = 10U};
static const aBusParam_t params[] = {
    {.type = ALIB_DATA_U8, .size = 1U, .range = &range},
    {.offset = 1U, .type = ALIB_DATA_RAW, .size = 3U}
};
static const aBusSig_t sigs[] = {
    {.type = ALIB_DATA_U8, .size = 1U, .range = &range},
    {.type = ALIB_DATA_U16, .size = 2U},
    {.type = ALIB_DATA_U32, .size = 4U},
    {.type = ALIB_DATA_S32, .size = 4U},
    {.type = ALIB_DATA_RAW, .size = 3U},
    {.type = ALIB_DATA_STRUCT, .size = 4U, .params = params,
     .param_count = 2U}
};
static const aBusTable_t table = {
    .deviceID = 90U, .sigs = sigs, .sig_count = 6U
};
static aBusHandle_t handle;
static aBusSigState_t states[6];

aStatus_t sigDataGetInfo(const aBusSigQuery_t *q, aBusSigInfo_t *info)
{
    return aBusGetSigInfo(&handle, q, info);
}
aStatus_t sigDataSet(const aBusSetIndexRequest_t *r)
{
    return aBusSetByIndex(&handle, r);
}
aStatus_t sigDataGet(const aBusGetIndexRequest_t *r)
{
    return aBusGetByIndex(&handle, r);
}
aStatus_t sigDataSetParam(const aBusSetParamRequest_t *r)
{
    return aBusSetParam(&handle, r);
}
aStatus_t sigDataGetParam(const aBusGetParamRequest_t *r)
{
    return aBusGetParam(&handle, r);
}

int test_command(int argc, char **argv);
static int run(int argc, char **argv)
{
    int result;

    output_used = 0U;
    output[0] = '\0';
    result = test_command(argc, argv);
    assert(allocations == 0);
    return result;
}

int main(void)
{
    char *set[] = {"sig", "set", "90", "0", "10"};
    char *all[] = {"sig", "get", "90"};
    char *get[] = {"sig", "get", "90", "0"};
    char *field[] = {"sig", "set", "90", "5", "1", "aB01fF"};
    char *read_field[] = {"sig", "get", "90", "5", "1"};

    assert(strcmp(aDataTypeName(ALIB_DATA_U8), "U8") == 0);
    assert(strcmp(aDataTypeName(ALIB_DATA_U16), "U16") == 0);
    assert(strcmp(aDataTypeName(ALIB_DATA_U32), "U32") == 0);
    assert(strcmp(aDataTypeName(ALIB_DATA_S32), "S32") == 0);
    assert(strcmp(aDataTypeName(ALIB_DATA_RAW), "RAW") == 0);
    assert(strcmp(aDataTypeName(ALIB_DATA_STRUCT), "STRUCT") == 0);
    assert(strcmp(aDataTypeName((aDataType_t)-1), "UNKNOWN") == 0);
    assert(strcmp(aBusSigFlagName(ABUS_SIG_FLAG_LOCK), "LOCK") == 0);
    assert(strcmp(aBusSigFlagName(0x8000U), "UNKNOWN") == 0);
    aBusInstanceStructInit(&handle, states, 6U);
    assert(aBusInitStatic(0U, &table, 1U, &handle) == A_STATUS_OK);
    assert(run(5, set) == 0 && u8 == 10U);
    set[4] = "11";
    assert(run(5, set) == -1 && u8 == 10U);
    set[3] = "1"; set[4] = "65535";
    assert(run(5, set) == 0 && u16 == UINT16_MAX);
    set[3] = "2"; set[4] = "4294967295";
    assert(run(5, set) == 0 && u32 == UINT32_MAX);
    set[3] = "3"; set[4] = "-2147483648";
    assert(run(5, set) == 0 && s32 == INT32_MIN);
    get[3] = "3";
    assert(run(4, get) == 0 && strstr(output, "-2147483648") != NULL);
    set[3] = "4"; set[4] = "aB01fF";
    assert(run(5, set) == 0 && raw[0] == 0xAB && raw[2] == 0xFF);
    get[3] = "4";
    assert(run(4, get) == 0 && strstr(output, "AB01FF") != NULL);
    set[4] = "00000Z";
    assert(run(5, set) == -1 && raw[0] == 0xAB);
    set[4] = "000";
    assert(run(5, set) == -1 && raw[0] == 0xAB);
    set[4] = "0000";
    assert(run(5, set) == -1 && raw[0] == 0xAB);
    assert(run(6, field) == 0 && group[1] == 0xAB && group[0] == 0U);
    assert(run(5, read_field) == 0 && strstr(output, "AB01FF") != NULL);
    get[3] = "5";
    assert(run(4, get) == 0);
    assert(strstr(output, "param[0]=0") != NULL);
    assert(strstr(output, "param[1]=AB01FF") != NULL);
    field[4] = "0"; field[5] = "11";
    assert(run(6, field) == -1 && group[0] == 0U);
    set[3] = "5";
    assert(run(5, set) == -1);
    assert(run(3, all) == 0);
    assert(strstr(output, "sig[90:0]=10") != NULL);
    assert(strstr(output, "type=U8 size=1 flags=0x0000 (NONE)") != NULL);
    assert(strstr(output, "range=[0,10]") != NULL);
    assert(strstr(output, "type=STRUCT size=4") != NULL);
    assert(strstr(output, "param[1] type=RAW offset=1 size=3") != NULL);
    assert(strstr(output, "range=none") != NULL);
    assert(strstr(output, "sig[90:1]=65535") != NULL);
    assert(strstr(output, "sig[90:2]=4294967295") != NULL);
    assert(strstr(output, "sig[90:3]=-2147483648") != NULL);
    assert(strstr(output, "sig[90:4]=AB01FF") != NULL);
    assert(strstr(output, "sig[90:5] (2 params)") != NULL);
    assert(strstr(output, "param[1]=AB01FF") != NULL);
    all[2] = "91";
    assert(run(3, all) == -1);
    all[2] = "-1";
    assert(run(3, all) == -1);
    all[2] = "90";
    allocation_fail = 1;
    assert(run(3, all) == -1);
    assert(run(4, get) == -1 && group[1] == 0xAB);
    allocation_fail = 0;
    get[2] = "65536";
    assert(run(4, get) == -1);
    assert(aBusDeInitStatic(&handle) == A_STATUS_OK);
    return 0;
}
