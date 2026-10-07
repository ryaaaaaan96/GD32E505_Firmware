#include "app_database.h"
#include "aMemory_layout.h"
#include "aShell.h"
#include <errno.h>
#include <inttypes.h>
#include <string.h>

static aStatus_t timestamp_parse(const char *text, aDataBaseTime_t *value)
{
    const char *digit = text;
    char *end;
    intmax_t result;

    if (*digit == '\0') return A_STATUS_INVALID_PARAM;
    while (*digit != '\0') {
        if (*digit < '0' || *digit > '9') return A_STATUS_INVALID_PARAM;
        ++digit;
    }
    errno = 0;
    result = strtoimax(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || result > INT64_MAX)
        return A_STATUS_INVALID_PARAM;
    *value = (aDataBaseTime_t)result;
    return A_STATUS_OK;
}

/* 转成字符串后打印，兼容板端精简 C 库的整数格式化能力。 */
static const char *timestamp_text(aDataBaseTime_t value, char buffer[21])
{
    aBool_t negative = value < 0;
    uint64_t magnitude = negative ? (uint64_t)(-(value + 1)) + 1U
                                  : (uint64_t)value;
    size_t position = 20U;

    buffer[position] = '\0';
    do {
        buffer[--position] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U);
    if (negative) buffer[--position] = '-';
    return buffer + position;
}

/* Shell 可写文本，公共接口可写二进制；读取同时显示长度和十六进制。 */
static void data_print(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    aBool_t printable = A_TRUE;
    char hex[65];
    const char digits[] = "0123456789ABCDEF";

    for (size_t i = 0U; i < size; ++i)
        if (bytes[i] < 32U || bytes[i] > 126U) printable = A_FALSE;
    ASHELL_PRINT("%lu bytes", (unsigned long)size);
    ASHELL_PRINT("\r\n");
    if (printable) {
        for (size_t offset = 0U; offset < size; offset += 64U) {
            size_t count = size - offset;

            if (count > 64U) count = 64U;
            ASHELL_PRINT("%.*s\r\n", (int)count,
                         (const char *)data + offset);
        }
    }
    for (size_t offset = 0U; offset < size; offset += 32U) {
        size_t count = size - offset;

        if (count > 32U) count = 32U;
        for (size_t i = 0U; i < count; ++i) {
            hex[i * 2U] = digits[bytes[offset + i] >> 4];
            hex[i * 2U + 1U] = digits[bytes[offset + i] & 15U];
        }
        hex[count * 2U] = '\0';
        ASHELL_PRINT("%s\r\n", hex);
    }
}

static aStatus_t kv_command(int argc, char **argv)
{
    aDataBaseKvSetRequest_t set;
    aDataBaseKvGetRequest_t get;
    aDataBaseKvDeleteRequest_t del;
    uint8_t buffer[256];
    size_t size;
    aStatus_t status;

    if (argc == 5 && strcmp(argv[2], "set") == 0) {
        aDataBaseKvSetRequestStructInit(&set);
        set.key = argv[3];
        set.data = argv[4];
        set.size = strlen(argv[4]);
        return appDatabaseKvSet(&set);
    }
    if (argc == 4 && strcmp(argv[2], "get") == 0) {
        aDataBaseKvGetRequestStructInit(&get);
        get.key = argv[3];
        get.data = buffer;
        get.capacity = sizeof(buffer);
        get.size_out = &size;
        status = appDatabaseKvGet(&get);
        if (status == A_STATUS_OK) data_print(buffer, size);
        if (status == A_STATUS_NO_MEMORY)
            ASHELL_PRINT("Value requires %lu bytes\r\n",
                         (unsigned long)size);
        return status;
    }
    if (argc == 4 && strcmp(argv[2], "del") == 0) {
        aDataBaseKvDeleteRequestStructInit(&del);
        del.key = argv[3];
        return appDatabaseKvDelete(&del);
    }
    return A_STATUS_INVALID_PARAM;
}

typedef struct {
    size_t count;
    size_t output_bytes;
    aDataBaseTime_t last_timestamp;
    aBool_t limited;
} print_context_t;

static aBool_t record_print(const aDataBaseTsRecord_t *record, void *context)
{
    print_context_t *print = context;
    size_t output_bound = record->size * 3U + 80U;
    char time[21];

    /* 给提示和并发日志预留空间，避免一次列表压满输出队列。 */
    if (print->output_bytes + output_bound > 900U) {
        print->limited = A_TRUE;
        return A_FALSE;
    }
    ASHELL_PRINT("time=%s ", timestamp_text(record->timestamp, time));
    data_print(record->data, record->size);
    ++print->count;
    print->output_bytes += output_bound;
    print->last_timestamp = record->timestamp;
    print->limited = print->count >= 8U;
    return !print->limited;
}

static aStatus_t ts_command(int argc, char **argv)
{
    aDataBaseTsAppendRequest_t append;
    aDataBaseTsIterateRequest_t iterate;
    uint8_t buffer[256];
    print_context_t print = { 0U, 0U, 0, A_FALSE };
    char from[21], to[21];
    aStatus_t status;

    if (argc == 5 && strcmp(argv[2], "append") == 0) {
        aDataBaseTsAppendRequestStructInit(&append);
        status = timestamp_parse(argv[3], &append.timestamp);
        if (status != A_STATUS_OK) return status;
        append.data = argv[4];
        append.size = strlen(argv[4]);
        return appDatabaseTsAppend(&append);
    }
    if ((argc == 3 || argc == 5) && strcmp(argv[2], "list") == 0) {
        aDataBaseTsIterateRequestStructInit(&iterate);
        if (argc == 5) {
            status = timestamp_parse(argv[3], &iterate.from);
            if (status != A_STATUS_OK) return status;
            status = timestamp_parse(argv[4], &iterate.to);
            if (status != A_STATUS_OK) return status;
        }
        iterate.buffer = buffer;
        iterate.capacity = sizeof(buffer);
        iterate.callback = record_print;
        iterate.context = &print;
        status = appDatabaseTsIterate(&iterate);
        ASHELL_PRINT("%lu records shown\r\n", (unsigned long)print.count);
        if (print.limited && print.last_timestamp < INT64_MAX)
            ASHELL_PRINT("Continue: db ts list %s %s\r\n",
                timestamp_text(print.last_timestamp + 1, from),
                timestamp_text(iterate.to, to));
        return status;
    }
    return A_STATUS_INVALID_PARAM;
}

static void usage(void)
{
    ASHELL_PRINT("db init | close | info\r\n"
                 "db kv set <key> <text>\r\n"
                 "db kv get <key>\r\n"
                 "db kv del <key>\r\n"
                 "db ts append <timestamp> <text>\r\n"
                 "db ts list [from to]\r\n");
}

static int database_command(int argc, char **argv)
{
    aDataBaseTsInfo_t info;
    aStatus_t status = A_STATUS_INVALID_PARAM;
    char time[21];

    if (argc == 2 && strcmp(argv[1], "init") == 0) {
        ASHELL_PRINT("Open database partitions; invalid headers may be "
                     "formatted.\r\n");
        status = appDatabaseOpen(A_TRUE);
    } else if (argc == 2 && strcmp(argv[1], "close") == 0) {
        status = appDatabaseClose();
    } else if (argc == 2 && strcmp(argv[1], "info") == 0) {
        ASHELL_PRINT("KV: 0x%08lX, %lu bytes; TSDB: 0x%08lX, "
                     "%lu bytes\r\n",
                     (unsigned long)AMEMORY_PART_PARAM_OFFSET,
                     (unsigned long)AMEMORY_PART_PARAM_SIZE,
                     (unsigned long)AMEMORY_PART_LOG_OFFSET,
                     (unsigned long)AMEMORY_PART_LOG_SIZE);
        status = appDatabaseTsGetInfo(&info);
        if (status == A_STATUS_OK)
            ASHELL_PRINT("last_time=%s, max_record=%lu, "
                         "rollover=%u\r\n",
                         timestamp_text(info.last_timestamp, time),
                         (unsigned long)info.max_record_size,
                         (unsigned)info.rollover);
    } else if (argc >= 3 && strcmp(argv[1], "kv") == 0) {
        status = kv_command(argc, argv);
    } else if (argc >= 3 && strcmp(argv[1], "ts") == 0) {
        status = ts_command(argc, argv);
    } else {
        usage();
    }
    if (status == A_STATUS_OK) ASHELL_PRINT("OK\r\n");
    else ASHELL_PRINT("Database command failed: %d\r\n", (int)status);
    return status == A_STATUS_OK ? 0 : -1;
}

ASHELL_CMD_EXPORT(db, database_command, "KV and time-series storage");
