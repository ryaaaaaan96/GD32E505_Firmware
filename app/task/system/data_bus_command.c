#include "data_bus_service.h"
#include "aShell.h"
#include "aOS.h"
#include <errno.h>
#include <inttypes.h>
#include <string.h>

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

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static aStatus_t text_parse(aDataType_t type, const char *text,
                            void *data, size_t size)
{
    int64_t number;
    aDataValue_t value;

    if (type == ALIB_DATA_RAW) {
        size_t length = strlen(text);
        unsigned char *bytes = data;

        if (length % 2U != 0U || length / 2U != size) {
            return A_STATUS_INVALID_PARAM;
        }
        for (size_t i = 0; i < size; i++) {
            int high = hex_digit(text[i * 2U]);
            int low = hex_digit(text[i * 2U + 1U]);

            if (high < 0 || low < 0) return A_STATUS_INVALID_PARAM;
            bytes[i] = (unsigned char)((high << 4) | low);
        }
        return A_STATUS_OK;
    }
    if (size != aDataTypeSize(type) ||
        number_parse(text, &number) != A_STATUS_OK) {
        return A_STATUS_INVALID_PARAM;
    }
    switch (type) {
    case ALIB_DATA_U8:
        if (number < 0 || number > UINT8_MAX) break;
        value.u8 = (uint8_t)number;
        memcpy(data, &value.u8, size);
        return A_STATUS_OK;
    case ALIB_DATA_U16:
        if (number < 0 || number > UINT16_MAX) break;
        value.u16 = (uint16_t)number;
        memcpy(data, &value.u16, size);
        return A_STATUS_OK;
    case ALIB_DATA_U32:
        if (number < 0 || number > UINT32_MAX) break;
        value.u32 = (uint32_t)number;
        memcpy(data, &value.u32, size);
        return A_STATUS_OK;
    case ALIB_DATA_S32:
        if (number < INT32_MIN || number > INT32_MAX) break;
        value.s32 = (int32_t)number;
        memcpy(data, &value.s32, size);
        return A_STATUS_OK;
    default: break;
    }
    return A_STATUS_INVALID_PARAM;
}

/* 数据可能是结构体中的非对齐字段，不直接解引用整数指针。 */
static void value_print(aDataType_t type, const void *data, size_t size)
{
    aDataValue_t value;
    unsigned long number;

    if (type == ALIB_DATA_RAW) {
        const unsigned char *bytes = data;
        const char hex[] = "0123456789ABCDEF";
        char line[65];
        size_t used = 0U;

        for (size_t i = 0; i < size; i++) {
            line[used++] = hex[bytes[i] >> 4];
            line[used++] = hex[bytes[i] & 15U];
            if (used == sizeof(line) - 1U || i + 1U == size) {
                line[used] = '\0';
                ASHELL_REPLY("%s", line);
                used = 0U;
            }
        }
        ASHELL_REPLY("\r\n");
        return;
    }
    if (size != aDataTypeSize(type)) {
        ASHELL_REPLY("invalid definition\r\n");
        return;
    }
    switch (type) {
    case ALIB_DATA_U8:
        memcpy(&value.u8, data, size);
        number = value.u8;
        break;
    case ALIB_DATA_U16:
        memcpy(&value.u16, data, size);
        number = value.u16;
        break;
    case ALIB_DATA_U32:
        memcpy(&value.u32, data, size);
        number = value.u32;
        break;
    case ALIB_DATA_S32:
        memcpy(&value.s32, data, size);
        ASHELL_REPLY("%ld\r\n", (long)value.s32);
        return;
    default:
        ASHELL_REPLY("unsupported type\r\n");
        return;
    }
    ASHELL_REPLY("%lu\r\n", number);
}

static void range_print(aDataType_t type, const aBusRange_t *range)
{
    unsigned long minimum;
    unsigned long maximum;

    if (range == NULL) {
        ASHELL_REPLY(" range=none\r\n");
        return;
    }
    switch (type) {
    case ALIB_DATA_U8:
        minimum = range->min.u8;
        maximum = range->max.u8;
        break;
    case ALIB_DATA_U16:
        minimum = range->min.u16;
        maximum = range->max.u16;
        break;
    case ALIB_DATA_U32:
        minimum = range->min.u32;
        maximum = range->max.u32;
        break;
    case ALIB_DATA_S32:
        ASHELL_REPLY(" range=[%ld,%ld]\r\n", (long)range->min.s32,
                     (long)range->max.s32);
        return;
    default:
        ASHELL_REPLY(" range=invalid\r\n");
        return;
    }
    ASHELL_REPLY(" range=[%lu,%lu]\r\n", minimum, maximum);
}

static void sig_info_print(const aBusSigQuery_t *query,
                           const aBusSigInfo_t *info)
{
    ASHELL_REPLY("sig[%u:%u] index=%lu type=%s size=%lu flags=0x%04X (",
                 (unsigned)query->deviceID, (unsigned)info->sigKey,
                 (unsigned long)query->sigIndex, aDataTypeName(info->type),
                 (unsigned long)info->size, (unsigned)info->flags);
    if (info->flags == 0U) {
        ASHELL_REPLY("%s", aBusSigFlagName(0U));
    } else {
        aBool_t first = A_TRUE;

        for (unsigned bit = 0U; bit < 16U; bit++) {
            uint16_t flag = (uint16_t)(1U << bit);

            if ((info->flags & flag) == 0U) continue;
            ASHELL_REPLY("%s%s", first ? "" : "|", aBusSigFlagName(flag));
            first = A_FALSE;
        }
    }
    ASHELL_REPLY(") params=%lu", (unsigned long)info->param_count);
    range_print(info->type, info->range);
}

static void param_info_print(size_t index, const aBusParam_t *param)
{
    ASHELL_REPLY("  param[%lu] type=%s offset=%lu size=%lu",
                 (unsigned long)index, aDataTypeName(param->type),
                 (unsigned long)param->offset, (unsigned long)param->size);
    range_print(param->type, param->range);
}

static aStatus_t command_read(const aBusSigQuery_t *query,
    aBool_t field, size_t index, void *data, size_t size)
{
    if (field) {
        aBusGetParamRequest_t request;

        aBusGetParamRequestStructInit(&request);
        request.deviceID = query->deviceID;
        request.sigIndex = query->sigIndex;
        request.paramIndex = index;
        request.dst = data;
        request.size = size;
        return dataBusGetParam(&request);
    } else {
        aBusGetIndexRequest_t request;

        aBusGetIndexRequestStructInit(&request);
        request.deviceID = query->deviceID;
        request.sigIndex = query->sigIndex;
        request.dst = data;
        request.size = size;
        return dataBusGet(&request);
    }
}

static aStatus_t command_write(const aBusSigQuery_t *query,
    aBool_t field, size_t index, const void *data, size_t size)
{
    if (field) {
        aBusSetParamRequest_t request;

        aBusSetParamRequestStructInit(&request);
        request.deviceID = query->deviceID;
        request.sigIndex = query->sigIndex;
        request.paramIndex = index;
        request.src = data;
        request.size = size;
        return dataBusSetParam(&request);
    } else {
        aBusSetIndexRequest_t request;

        aBusSetIndexRequestStructInit(&request);
        request.deviceID = query->deviceID;
        request.sigIndex = query->sigIndex;
        request.src = data;
        request.size = size;
        return dataBusSet(&request);
    }
}

static int command_sig(int argc, char **argv)
{
    aBusSigQuery_t query;
    aBusSigKeyQuery_t key;
    aBusSigInfo_t info;
    aDataType_t type;
    aStatus_t status;
    int64_t id = 0;
    int64_t device;
    int64_t param = 0;
    size_t size;
    void *data = NULL;
    aBool_t writing;
    aBool_t field;
    aBool_t all;

    if (argc < 3 || argc > 6) goto usage;
    writing = strcmp(argv[1], "set") == 0;
    if ((!writing && strcmp(argv[1], "get") != 0) ||
        (writing && argc < 5) || (!writing && argc > 5)) goto usage;
    all = !writing && argc == 3;
    field = argc == (writing ? 6 : 5);
    if (number_parse(argv[2], &device) != A_STATUS_OK ||
        device < 0 || device > UINT16_MAX ||
        (!all && number_parse(argv[3], &id) != A_STATUS_OK) ||
        id < 0 || id > UINT16_MAX ||
        (field && (number_parse(argv[4], &param) != A_STATUS_OK ||
                   param < 0 || (uint64_t)param > SIZE_MAX))) goto usage;
    query.deviceID = (uint16_t)device;
    query.sigIndex = 0U;
    if (!all) {
        /* 外部使用稳定键，解析一次后复用下标查询定义及当前值。 */
        key.deviceID = query.deviceID;
        key.sigKey = (uint16_t)id;
        status = dataBusResolveKey(&key, &query.sigIndex);
        if (status != A_STATUS_OK) goto done;
    }
next_sig:
    data = NULL;
    status = dataBusGetInfo(&query, &info);
    /* 表内下标连续；首项不存在是未知设备，后续不存在表示遍历结束。 */
    if (all && query.sigIndex != 0U && status == A_STATUS_NOT_FOUND) {
        return 0;
    }
    if (status != A_STATUS_OK) goto done;
    type = info.type;
    size = info.size;
    if (field) {
        if (type != ALIB_DATA_STRUCT) {
            status = A_STATUS_UNSUPPORTED;
            goto done;
        }
        if ((size_t)param >= info.param_count) {
            status = A_STATUS_NOT_FOUND;
            goto done;
        }
        type = info.params[param].type;
        size = info.params[param].size;
    }
    if (writing && type == ALIB_DATA_STRUCT) goto usage;
    if (size == 0U) {
        status = A_STATUS_INVALID_PARAM;
        goto done;
    }
    /* 整组读取只取一次快照，避免显示各字段时混用不同次更新。 */
    data = aOSAlloc(size);
    if (data == NULL) {
        status = A_STATUS_NO_MEMORY;
        goto done;
    }
    if (writing) {
        status = text_parse(type, argv[argc - 1], data, size);
        if (status == A_STATUS_OK) {
            status = command_write(&query, field, (size_t)param, data, size);
        }
    } else {
        status = command_read(&query, field, (size_t)param, data, size);
    }
    if (status != A_STATUS_OK) goto done;
    if (!writing) sig_info_print(&query, &info);
    if (type == ALIB_DATA_STRUCT) {
        ASHELL_REPLY("sig[%u:%u] (%lu params)\r\n",
                     (unsigned)query.deviceID, (unsigned)info.sigKey,
                     (unsigned long)info.param_count);
        for (size_t i = 0; i < info.param_count; i++) {
            const aBusParam_t *item = &info.params[i];

            param_info_print(i, item);
            ASHELL_REPLY("  param[%lu]=", (unsigned long)i);
            value_print(item->type,
                        (const unsigned char *)data + item->offset,
                        item->size);
        }
    } else {
        if (!writing && field) {
            param_info_print((size_t)param, &info.params[param]);
        }
        ASHELL_REPLY("sig[%u:%u]", (unsigned)query.deviceID,
                     (unsigned)info.sigKey);
        if (field) ASHELL_REPLY(".param[%lu]", (unsigned long)param);
        ASHELL_REPLY("=");
        value_print(type, data, size);
    }
done:
    aOSFree(data);
    if (status != A_STATUS_OK) {
        ASHELL_REPLY("sig failed: %d\r\n", (int)status);
        return -1;
    }
    if (all) {
        query.sigIndex++;
        goto next_sig;
    }
    return 0;
usage:
    ASHELL_REPLY("sig get <deviceID> [sigKey [paramIndex]]\r\n"
                 "sig set <deviceID> <sigKey> [paramIndex] <value>\r\n"
                 "RAW: exact-length hex; STRUCT: set a param\r\n");
    return -1;
}
ASHELL_CMD_EXPORT(sig, command_sig, "Read/write SIG by stable key");
