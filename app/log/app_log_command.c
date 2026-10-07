#include "aLog.h"
#include "aShell.h"
#include <string.h>

static const char *const level_names[] = {
    "", "error", "warn", "info", "debug", "verbose"
};

static aStatus_t level_parse(const char *name, aLogLevel_t *level)
{
    for (unsigned i = ALOG_LEVEL_ERROR; i <= ALOG_LEVEL_VERBOSE; ++i) {
        if (strcmp(name, level_names[i]) == 0) {
            *level = (aLogLevel_t)i;
            return A_STATUS_OK;
        }
    }
    return A_STATUS_INVALID_PARAM;
}

static void usage_print(void)
{
    ASHELL_PRINT("log test | info | level [error|warn|info|debug|verbose]"
                 "\r\n");
    ASHELL_PRINT("log write <level> <tag> <text...>\r\n");
}

/* nr 按空格拆分参数，日志正文将剩余参数合并成一条消息。 */
static aStatus_t write_output(int argc, char **argv)
{
    char text[ALOG_LINE_BUFFER_SIZE - 80U];
    size_t used = 0U;
    aLogLevel_t level;
    aStatus_t status = level_parse(argv[2], &level);

    if (status != A_STATUS_OK) return status;
    for (int i = 4; i < argc; ++i) {
        size_t size = strlen(argv[i]);
        size_t separator = i == 4 ? 0U : 1U;

        if (size + separator >= sizeof(text) - used)
            return A_STATUS_INVALID_PARAM;
        if (separator != 0U) text[used++] = ' ';
        memcpy(text + used, argv[i], size);
        used += size;
    }
    text[used] = '\0';
    return aLogWrite(level, argv[3], "%s", text);
}

static aStatus_t test_output(void)
{
    static const uint8_t data[] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x10U, 0x20U, 0x30U, 0x40U,
        0x41U, 0x49U, 0x44U, 0x43U, 0x7FU, 0x80U, 0xFEU, 0xFFU
    };
    aStatus_t status;

    for (unsigned i = ALOG_LEVEL_ERROR; i <= ALOG_LEVEL_VERBOSE; ++i) {
        status = aLogWrite((aLogLevel_t)i, "test", "%s message",
                           level_names[i]);
        if (status != A_STATUS_OK) return status;
    }
    return aLogHexDump("test", data, sizeof(data));
}

static int log_command(int argc, char **argv)
{
    aLogStats_t stats;
    aLogLevel_t level;
    aStatus_t status = A_STATUS_INVALID_PARAM;

    if (argc == 2 && strcmp(argv[1], "test") == 0) {
        status = test_output();
    } else if (argc == 2 && strcmp(argv[1], "info") == 0) {
        status = aLogGetStats(&stats);
        if (status == A_STATUS_OK) {
            ASHELL_PRINT("level=%s, compiled=%u, output=%u, dropped=%u, "
                         "filtered=%u\r\n", level_names[stats.level],
                         (unsigned)ALOG_OUTPUT_LEVEL, stats.output_records,
                         stats.dropped_records, stats.filtered_records);
        }
    } else if (argc == 2 && strcmp(argv[1], "level") == 0) {
        status = aLogGetStats(&stats);
        if (status == A_STATUS_OK)
            ASHELL_PRINT("log level: %s\r\n", level_names[stats.level]);
    } else if (argc == 3 && strcmp(argv[1], "level") == 0) {
        status = level_parse(argv[2], &level);
        if (status == A_STATUS_OK) status = aLogSetLevel(level);
        if (status == A_STATUS_OK)
            ASHELL_PRINT("log level: %s\r\n", level_names[level]);
    } else if (argc >= 5 && strcmp(argv[1], "write") == 0) {
        status = write_output(argc, argv);
    } else {
        usage_print();
        return -1;
    }
    if (status != A_STATUS_OK) {
        ASHELL_PRINT("log failed: %d\r\n", (int)status);
        return -1;
    }
    return 0;
}

ASHELL_CMD_EXPORT(log, log_command, "Test logs, levels and output stats");
