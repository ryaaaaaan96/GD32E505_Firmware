/* 通用命令仅从这些测试点表查询；不链接任何产品点表。 */
#include "data_bus_service.h"
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

/* 两种编译顺序使用完全相同的 Shell 命令，键与下标互不依赖。 */
#if TEST_REORDER
enum { GROUP, RAW, S32, U32, U16, U8, SIG_COUNT };
#else
enum { U8, U16, U32, S32, RAW, GROUP, SIG_COUNT };
#endif
static uint8_t u8;
static uint16_t u16;
static uint32_t u32;
static int32_t s32;
static unsigned char raw[3];
static unsigned char group[4];
ABUS_RAM_BIND_EXPORT(t_u8, 0U, 90U, U8, u8);
ABUS_RAM_BIND_EXPORT(t_u16, 0U, 90U, U16, u16);
ABUS_RAM_BIND_EXPORT(t_u32, 0U, 90U, U32, u32);
ABUS_RAM_BIND_EXPORT(t_s32, 0U, 90U, S32, s32);
ABUS_RAM_BIND_EXPORT(t_raw, 0U, 90U, RAW, raw);
ABUS_RAM_BIND_EXPORT(t_group, 0U, 90U, GROUP, group);
static const aBusRange_t range = {.max.u8 = 10U};
static const aBusParam_t params[] = {
    {.type = ALIB_DATA_U8, .size = 1U, .range = &range},
    {.offset = 1U, .type = ALIB_DATA_RAW, .size = 3U}
};
static const aBusSig_t sigs[SIG_COUNT] = {
    [U8] = {.sigKey = 10U, .type = ALIB_DATA_U8, .size = 1U,
            .range = &range},
    [U16] = {.sigKey = 400U, .type = ALIB_DATA_U16, .size = 2U},
    [U32] = {.sigKey = 25U, .type = ALIB_DATA_U32, .size = 4U},
    [S32] = {.sigKey = 60000U, .type = ALIB_DATA_S32, .size = 4U},
    [RAW] = {.sigKey = 0U, .type = ALIB_DATA_RAW, .size = 3U},
    [GROUP] = {.sigKey = UINT16_MAX, .type = ALIB_DATA_STRUCT,
               .size = 4U, .params = params, .param_count = 2U}
};
static uint8_t other_u8;
ABUS_RAM_BIND_EXPORT(t_other, 0U, 91U, 0U, other_u8);
static const aBusSig_t other_sigs[] = {
    {.sigKey = 10U, .type = ALIB_DATA_U8, .size = 1U}
};
static const aBusTable_t tables[] = {
    {.deviceID = 90U, .sigs = sigs, .sig_count = SIG_COUNT},
    {.deviceID = 91U, .sigs = other_sigs, .sig_count = 1U}
};
static aBusHandle_t handle;
static aBusSigState_t states[SIG_COUNT + 1];

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
    dataBusConfig_t config;
    aBusSigKeyQuery_t key = {.deviceID = 90U, .sigKey = 400U};
    size_t index = SIZE_MAX;
    char *set[] = {"sig", "set", "90", "10", "10"};
    char *all[] = {"sig", "get", "90"};
    char *get[] = {"sig", "get", "90", "10"};
    char *field[] = {"sig", "set", "90", "65535", "1", "aB01fF"};
    char *read_field[] = {"sig", "get", "90", "65535", "1"};

    assert(strcmp(aDataTypeName(ALIB_DATA_U8), "U8") == 0);
    assert(strcmp(aDataTypeName(ALIB_DATA_U16), "U16") == 0);
    assert(strcmp(aDataTypeName(ALIB_DATA_U32), "U32") == 0);
    assert(strcmp(aDataTypeName(ALIB_DATA_S32), "S32") == 0);
    assert(strcmp(aDataTypeName(ALIB_DATA_RAW), "RAW") == 0);
    assert(strcmp(aDataTypeName(ALIB_DATA_STRUCT), "STRUCT") == 0);
    assert(strcmp(aDataTypeName((aDataType_t)-1), "UNKNOWN") == 0);
    assert(strcmp(aBusSigFlagName(ABUS_SIG_FLAG_LOCK), "LOCK") == 0);
    assert(strcmp(aBusSigFlagName(0x8000U), "UNKNOWN") == 0);

    assert(dataBusInit(NULL) == A_STATUS_INVALID_PARAM);
    assert(dataBusResolveKey(&key, &index) == A_STATUS_NOT_READY);
    assert(index == SIZE_MAX);
    aBusInstanceStructInit(&handle, states, SIG_COUNT + 1U);
    assert(aBusResolveKey(&handle, &key, &index) == A_STATUS_NOT_READY);
    assert(index == SIZE_MAX);
    dataBusConfigStructInit(&config);
    config.tables = tables;
    config.table_count = sizeof(tables) / sizeof(tables[0]);
    config.instance = &handle;
    assert(dataBusInit(&config) == A_STATUS_OK);
    assert(dataBusInit(&config) == A_STATUS_BUSY);
    assert(dataBusResolveKey(NULL, &index) == A_STATUS_INVALID_PARAM);
    assert(dataBusResolveKey(&key, NULL) == A_STATUS_INVALID_PARAM);
    assert(dataBusResolveKey(&key, &index) == A_STATUS_OK && index == U16);
    key.sigKey = 1234U;
    assert(dataBusResolveKey(&key, &index) == A_STATUS_NOT_FOUND);
    assert(index == U16);
    key.deviceID = 92U;
    assert(dataBusResolveKey(&key, &index) == A_STATUS_NOT_FOUND);
    assert(index == U16);
    assert(run(5, set) == 0 && u8 == 10U);
    set[4] = "11";
    assert(run(5, set) == -1 && u8 == 10U);
    set[3] = "400"; set[4] = "65535";
    assert(run(5, set) == 0 && u16 == UINT16_MAX);
    set[3] = "25"; set[4] = "4294967295";
    assert(run(5, set) == 0 && u32 == UINT32_MAX);
    set[3] = "60000"; set[4] = "-2147483648";
    assert(run(5, set) == 0 && s32 == INT32_MIN);
    get[3] = "60000";
    assert(run(4, get) == 0 && strstr(output, "-2147483648") != NULL);
    set[3] = "0"; set[4] = "aB01fF";
    assert(run(5, set) == 0 && raw[0] == 0xAB && raw[2] == 0xFF);
    get[3] = "0";
    assert(run(4, get) == 0 && strstr(output, "AB01FF") != NULL);
    set[4] = "00000Z";
    assert(run(5, set) == -1 && raw[0] == 0xAB);
    set[4] = "000";
    assert(run(5, set) == -1 && raw[0] == 0xAB);
    set[4] = "0000";
    assert(run(5, set) == -1 && raw[0] == 0xAB);
    assert(run(6, field) == 0 && group[1] == 0xAB && group[0] == 0U);
    assert(run(5, read_field) == 0 && strstr(output, "AB01FF") != NULL);
    get[3] = "65535";
    assert(run(4, get) == 0);
    assert(strstr(output, "param[0]=0") != NULL);
    assert(strstr(output, "param[1]=AB01FF") != NULL);
    field[4] = "0"; field[5] = "11";
    assert(run(6, field) == -1 && group[0] == 0U);
    set[3] = "65535";
    assert(run(5, set) == -1);
    assert(run(3, all) == 0);
    assert(strstr(output, "sig[90:10]=10") != NULL);
    assert(strstr(output, "type=U8 size=1 flags=0x0000 (NONE)") != NULL);
    assert(strstr(output, "range=[0,10]") != NULL);
    assert(strstr(output, "type=STRUCT size=4") != NULL);
    assert(strstr(output, "param[1] type=RAW offset=1 size=3") != NULL);
    assert(strstr(output, "range=none") != NULL);
    assert(strstr(output, "sig[90:400]=65535") != NULL);
    assert(strstr(output, "sig[90:25]=4294967295") != NULL);
    assert(strstr(output, "sig[90:60000]=-2147483648") != NULL);
    assert(strstr(output, "sig[90:0]=AB01FF") != NULL);
    assert(strstr(output, "sig[90:65535] (2 params)") != NULL);
    assert(strstr(output, "param[1]=AB01FF") != NULL);
    set[2] = "91"; set[3] = "10"; set[4] = "20";
    assert(run(5, set) == 0 && other_u8 == 20U && u8 == 10U);
    get[2] = "91"; get[3] = "10";
    assert(run(4, get) == 0 && strstr(output, "sig[91:10]=20") != NULL);
    get[2] = "90"; get[3] = "1234";
    assert(run(4, get) == -1);
    get[3] = "65536";
    assert(run(4, get) == -1);
    get[3] = "-1";
    assert(run(4, get) == -1);
    get[3] = "65535";
    all[2] = "92";
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
