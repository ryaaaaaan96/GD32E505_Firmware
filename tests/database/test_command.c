#define main database_fixture_main
#include "test_database.c"
#undef main
#include "database_service.h"
#include <stdarg.h>

int test_database_command(int argc, char **argv);
static size_t output_bytes;

int test_shell_print(const char *format, ...)
{
    char buffer[256];
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    assert(length >= 0 && (size_t)length < sizeof(buffer));
    output_bytes += (size_t)length;
    return length;
}

static int command(int count, char **arguments)
{
    output_bytes = 0U;
    int result = test_database_command(count, arguments);
    assert(output_bytes <= 1024U);
    return result;
}

int main(void)
{
    aDataBaseTsInfo_t info;
    aDataBaseTsAppendRequest_t append;
    unsigned before;
    char *init[] = { "db", "init" };
    char *close[] = { "db", "close" };
    char *get[] = { "db", "kv", "get", "demo" };
    char *set[] = { "db", "kv", "set", "demo", "hello" };
    char *ts_append[] = { "db", "ts", "append", "5000000001", "sample" };
    char *list[] = { "db", "ts", "list" };
    char *range[] = { "db", "ts", "list", "5000000002", "5000000001" };
    const char *invalid[] = { "-1", "+2", "0", "1x", " 1",
                             "9223372036854775808" };

    memset(memory, 0xFF, sizeof(memory));
    assert(memory_start() == A_STATUS_OK);
    assert(appDatabaseInit() == A_STATUS_OK && erases == 0U);
    assert(command(4, get) == -1);
    assert(command(2, init) == 0);
    assert(command(5, set) == 0);
    assert(command(4, get) == 0);
    assert(command(5, ts_append) == 0);
    assert(command(5, ts_append) == -1);
    before = writes;
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        ts_append[3] = (char *)invalid[i];
        assert(command(5, ts_append) == -1);
    }
    assert(writes == before);
    assert(command(5, range) == -1);
    assert(command(3, list) == 0);
    aDataBaseTsAppendRequestStructInit(&append);
    append.timestamp = INT64_C(5000000002);
    append.data = "sensor";
    append.size = 6U;
    for (int i = 0; i < 20; ++i) {
        assert(appDatabaseTsAppend(&append) == A_STATUS_OK);
        ++append.timestamp;
    }
    assert(command(3, list) == 0);
    /* 最大长度文本与二进制记录均不能超出打印缓冲区和单次输出预算。 */
    append.data = memory;
    append.size = 256U;
    assert(appDatabaseTsAppend(&append) == A_STATUS_OK);
    range[3] = "5000000022";
    range[4] = "5000000022";
    assert(command(5, range) == 0);
    before = erases;
    assert(command(2, close) == 0);
    assert(command(2, init) == 0 && erases == before);
    assert(command(4, get) == 0);
    assert(appDatabaseTsGetInfo(&info) == A_STATUS_OK);
    assert(info.last_timestamp == append.timestamp);
    assert(command(2, close) == 0);
    assert(aDataBaseDeInit() == A_STATUS_OK);
    assert(aMemoryDeInit() == A_STATUS_OK);
    assert(database_allocations == 0U);
    puts("数据库 Shell 命令及输出边界验证通过");
    return 0;
}
