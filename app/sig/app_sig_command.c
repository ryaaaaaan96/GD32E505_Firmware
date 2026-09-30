#include "app_sig.h"
#include "aShell.h"
#include <errno.h>
#include <inttypes.h>
#include <string.h>

/* 下标对应 sigIndex；RAW 暂不提供结构体的文本解析。 */
static const aDataType_t command_types[APP_BUS_SIG_COUNT] = {
    [APP_BUS_MOTOR] = ALIB_DATA_RAW,
    [APP_BUS_COUNTER] = ALIB_DATA_U32
};

static aStatus_t number_parse(const char *text, int64_t *value)
{
    char *end;
    intmax_t result;
    const char *digit = text;

    if (*digit == '-') digit++;
    if (*digit == '\0') return A_STATUS_INVALID_PARAM;
    while (*digit != '\0') {
        if (*digit < '0' || *digit > '9') return A_STATUS_INVALID_PARAM;
        digit++;
    }
    errno = 0;
    result = strtoimax(text, &end, 10);
    if (errno == ERANGE || *end != '\0') return A_STATUS_INVALID_PARAM;
    *value = result;
    return A_STATUS_OK;
}

static int command_sig(int argc, char **argv)
{
    aBusSetIndexRequest_t set_request;
    aBusGetIndexRequest_t get_request;
    aDataValue_t value;
    aDataType_t type;
    aStatus_t status;
    int64_t number = 0;
    int64_t id;
    int64_t minimum = 0;
    int64_t maximum;
    void *data;
    size_t size;
    aBool_t writing;

    if (argc < 3 || argc > 4) goto usage;
    writing = strcmp(argv[1], "set") == 0;
    if ((!writing && strcmp(argv[1], "get") != 0) ||
        argc != (writing ? 4 : 3)) goto usage;
    if (number_parse(argv[2], &id) != A_STATUS_OK ||
        id < 0 || id >= APP_BUS_SIG_COUNT) {
        ASHELL_PRINT("unknown sigID\r\n");
        return -1;
    }
    type = command_types[id];
    size = aDataTypeSize(type);
    switch (type) {
    case ALIB_DATA_U8:
        data = &value.u8;
        maximum = UINT8_MAX;
        break;
    case ALIB_DATA_U16:
        data = &value.u16;
        maximum = UINT16_MAX;
        break;
    case ALIB_DATA_U32:
        data = &value.u32;
        maximum = UINT32_MAX;
        break;
    case ALIB_DATA_S32:
        data = &value.s32;
        minimum = INT32_MIN;
        maximum = INT32_MAX;
        break;
    default:
        ASHELL_PRINT("sigID has no scalar text interface\r\n");
        return -1;
    }
    if (writing) {
        if (number_parse(argv[3], &number) != A_STATUS_OK ||
            number < minimum || number > maximum) {
            ASHELL_PRINT("invalid value\r\n");
            return -1;
        }
        switch (type) {
        case ALIB_DATA_U8: value.u8 = (uint8_t)number; break;
        case ALIB_DATA_U16: value.u16 = (uint16_t)number; break;
        case ALIB_DATA_U32: value.u32 = (uint32_t)number; break;
        case ALIB_DATA_S32: value.s32 = (int32_t)number; break;
        default: return -1;
        }
        aBusSetIndexRequestStructInit(&set_request);
        set_request.sigIndex = (size_t)id;
        set_request.src = data;
        set_request.size = size;
        status = appSigSet(&set_request);
    } else {
        aBusGetIndexRequestStructInit(&get_request);
        get_request.sigIndex = (size_t)id;
        get_request.dst = data;
        get_request.size = size;
        status = appSigGet(&get_request);
    }
    if (status != A_STATUS_OK) {
        ASHELL_PRINT("sig failed: %d\r\n", (int)status);
        return -1;
    }
    if (type == ALIB_DATA_S32) {
        ASHELL_PRINT("sig[%lu]=%ld\r\n", (unsigned long)id,
                     (long)value.s32);
    } else {
        switch (type) {
        case ALIB_DATA_U8: number = value.u8; break;
        case ALIB_DATA_U16: number = value.u16; break;
        default: number = value.u32; break;
        }
        ASHELL_PRINT("sig[%lu]=%lu\r\n", (unsigned long)id,
                     (unsigned long)number);
    }
    return 0;

usage:
    ASHELL_PRINT("usage: sig get <sigID> | sig set <sigID> <value>\r\n");
    return -1;
}
ASHELL_CMD_EXPORT(sig, command_sig, "Read/write scalar by sigIndex");
