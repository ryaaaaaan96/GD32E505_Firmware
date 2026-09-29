#include "aShell.h"
#include "aShell_config.h"
#include "aOS.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static unsigned allocations, mutexes, calls, lock_depth;
static aBool_t fail_alloc, fail_mutex;
static aErrno_t last_error;
static uint32_t now_ms;
static const char *input;
static aSSize_t forced_read = -99;
static char output[65536];
static size_t output_size, write_limit = 65536U;
static unsigned write_calls, write_mode;
static uint32_t budgets[65536];
static char last_arg[ASHELL_LINE_SIZE];
static int last_argc;

aErrno_t aOSGetErrno(void) { return last_error; }
aSSize_t aOSFailWithStatus(aStatus_t status)
{ last_error = aStatusToErrno(status); return -1; }
uint32_t aOSGetUptimeMs(void) { return now_ms; }
void *aOSAlloc(size_t size)
{
    void *p;
    if (fail_alloc) return NULL;
    p = malloc(size);
    if (p) ++allocations;
    return p;
}
void aOSFree(void *p) { if (p) { --allocations; free(p); } }
aStatus_t aOSRecursiveMutexCreate(aOSRecursiveMutex_t *p)
{
    if (fail_mutex) return A_STATUS_NO_MEMORY;
    ++mutexes;
    *p = p;
    return A_STATUS_OK;
}
void aOSRecursiveMutexDestroy(aOSRecursiveMutex_t *p)
{ if (*p) { --mutexes; *p = NULL; } }
aStatus_t aOSRecursiveMutexLock(aOSRecursiveMutex_t p, aTimeout_t timeout)
{ (void)timeout; assert(p); ++lock_depth; return A_STATUS_OK; }
aStatus_t aOSRecursiveMutexUnlock(aOSRecursiveMutex_t p)
{ assert(p && lock_depth); --lock_depth; return A_STATUS_OK; }

static aSSize_t read_input(void *p, size_t n, aTimeout_t timeout)
{
    size_t count;
    assert(n == 64);
    assert(timeout.milliseconds == 20U);
    if (forced_read != -99) return forced_read;
    count = input ? strlen(input) : 0U;
    if (count > n) count = n;
    if (count) { memcpy(p, input, count); input += count; }
    return (aSSize_t)count;
}
static aSSize_t write_output(const void *p, size_t n, aTimeout_t timeout)
{
    assert(write_calls < 65536);
    budgets[write_calls++] = timeout.milliseconds;
    if (write_mode == 1) return 0;
    if (write_mode == 2 || (write_mode == 5 && write_calls > 1))
        return aOSFailWithStatus(A_STATUS_ERROR);
    if (write_mode == 3) return (aSSize_t)n + 1;
    if (write_mode == 4) return -2;
    if (n > write_limit) n = write_limit;
    assert(output_size + n < sizeof(output));
    memcpy(output + output_size, p, n);
    output_size += n;
    output[output_size] = '\0';
    now_ms += 4U;
    return (aSSize_t)n;
}
static void reset_output(void)
{
    output_size = 0;
    output[0] = '\0';
    write_calls = 0;
    write_mode = 0;
    write_limit = sizeof(output);
}
static void feed(const char *text)
{
    input = text;
    while (*input) assert(aShellProcess() == A_STATUS_OK);
}
static int capture(int argc, char **argv)
{
    assert(lock_depth == 1U);
    ++calls;
    last_argc = argc;
    strcpy(last_arg, argv[argc - 1]);
    ASHELL_PRINT("captured\r\n"); /* Recursive output lock. */
    return 0;
}
ASHELL_CMD_EXPORT(capture, capture, "Record arguments");
ASHELL_CMD_EXPORT(cat, capture, "Alias");
ASHELL_CMD_EXPORT(cancel, capture, "Third ambiguous completion");

int main(void)
{
    aShellConfig_t config;
    char line[512];
    unsigned before;
    unsigned i;

    aShellConfigStructInit(&config);
    assert(aShellIsEnabled());
    assert(aShellProcess() == A_STATUS_NOT_READY);
    assert(aShellInit(&config) == A_STATUS_INVALID_PARAM);
    config.stream.read = read_input;
    config.stream.write = write_output;
    config.read_timeout = A_TIMEOUT_MS(20U);
    config.write_timeout = A_TIMEOUT_MS(20U);
#ifdef TEST_DUPLICATE
    assert(aShellInit(&config) == A_STATUS_INVALID_PARAM);
    assert(!allocations && !mutexes && !output_size);
    return 0;
#endif
    fail_mutex = A_TRUE;
    assert(aShellInit(&config) == A_STATUS_NO_MEMORY);
    fail_mutex = A_FALSE;
    fail_alloc = A_TRUE; /* Shell no longer allocates a command table. */
    write_mode = 1;
    assert(aShellInit(&config) == A_STATUS_BUSY);
    assert(!allocations && !mutexes);
    reset_output();
    assert(aShellInit(&config) == A_STATUS_OK);
    assert(strstr(output, ASHELL_PROMPT ": "));
    assert(aShellInit(&config) == A_STATUS_BUSY);
    assert(aShellProcess() == A_STATUS_BUSY);

    reset_output();
    write_mode = 5;
    input = "capture failed-output\n";
    assert(aShellProcess() == A_STATUS_ERROR);
    assert(calls == 1 && write_calls == 2);
    /* Nested command Print cannot clear the first Process output error. */
    reset_output();
    ASHELL_PRINT("recovered");
    assert(!strcmp(output, "recovered"));
    reset_output();
    calls = 0;
    feed("capture one\r\n");
    assert(calls == 1 && last_argc == 2 && !strcmp(last_arg, "one"));
    for (i = 0; i < 12; ++i) {
        snprintf(line, sizeof(line), "capture %u\n", i);
        feed(line);
    }
    feed("\x1b[A\n");
    assert(!strcmp(last_arg, "11")); /* History wraps and deduplicates. */
    feed("capture abc\x7f\x1b[DZ\n");
    assert(!strcmp(last_arg, "aZb"));
    feed("capture abc\x1b[D\x1b[D\x1b[3~\n");
    assert(!strcmp(last_arg, "ac"));
    feed("capt\t value\n");
    assert(!strcmp(last_arg, "value"));
    feed("capt tail\x1b[D\x1b[D\x1b[D\x1b[D\x1b[D\t\n");
    assert(!strcmp(last_arg, "tail")); /* Completion preserves suffix. */
    before = calls;
    feed("ca\t\n"); /* Three candidates: no NULL strcmp or dispatch. */
    assert(calls == before);
    strcpy(line, "capture");
    for (i = 0; i < ASHELL_ARGUMENT_COUNT; ++i) strcat(line, " x");
    strcat(line, "\n");
    feed(line);
    assert(calls == before); /* Reject excess final argument. */
    memset(line, 'x', ASHELL_LINE_SIZE + 8U);
    strcpy(line + ASHELL_LINE_SIZE + 8U, "capture\n");
    feed(line);
    assert(calls == before); /* Never execute overflow suffix. */
    strcpy(line, "capture ");
    memset(line + 8, 'a', ASHELL_LINE_SIZE - 9U);
    line[ASHELL_LINE_SIZE - 1U] = '\n';
    line[ASHELL_LINE_SIZE] = '\0';
    feed(line);
    assert(calls == before + 1U);
    feed("\x1b[A\n"); /* Long history entry. */
    assert(calls == before + 2U);
    reset_output();
    feed("help\nversion\nmissing\n");
    assert(strstr(output, "Record arguments"));
    assert(strstr(output, "nr_micro_shell 2.0.0"));
    assert(strstr(output, "can't find cmd missing"));

    forced_read = 65;
    assert(aShellProcess() == A_STATUS_ERROR);
    forced_read = -2;
    assert(aShellProcess() == A_STATUS_ERROR);
    forced_read = -1;
    last_error = A_EAGAIN;
    assert(aShellProcess() == A_STATUS_BUSY);
    last_error = A_ETIMEDOUT;
    assert(aShellProcess() == A_STATUS_TIMEOUT);
    last_error = A_EIO;
    assert(aShellProcess() == A_STATUS_ERROR);
    forced_read = -99;
    reset_output();
    write_limit = 2;
    ASHELL_PRINT("abcdef");
    assert(!strcmp(output, "abcdef"));
    assert(write_calls == 3 && budgets[0] == 20 && budgets[1] == 16);
    for (i = 1; i <= 5; ++i) {
        reset_output();
        write_limit = 2;
        write_mode = i;
        ASHELL_PRINT("abcdef");
        assert(write_calls == (i == 5 ? 2U : 1U));
    }
    reset_output();
    write_mode = 1;
    input = "x";
    assert(aShellProcess() == A_STATUS_BUSY);
    reset_output();
    assert(aShellDeInit() == A_STATUS_OK);
    assert(!allocations && !mutexes && !lock_depth);
    config.write_timeout = A_TIMEOUT_MS(5U);
    assert(aShellInit(&config) == A_STATUS_OK);
    reset_output();
    write_limit = 2;
    ASHELL_PRINT("abcdef");
    assert(output_size == 4 && write_calls == 2);
    reset_output();
    feed("capture fresh\n"); /* Reinit clears editor, history and CR state. */
    assert(!strcmp(last_arg, "fresh"));
    assert(aShellDeInit() == A_STATUS_OK);
    config.write_timeout = A_TIMEOUT_NO_WAIT;
    assert(aShellInit(&config) == A_STATUS_OK);
    reset_output();
    write_limit = 2;
    ASHELL_PRINT("abcdef");
    assert(output_size == 2 && write_calls == 1);
    aStreamStructInit(&config.stream);
    reset_output();
    ASHELL_PRINT("still bound");
    assert(!strcmp(output, "still bound"));
    assert(aShellDeInit() == A_STATUS_OK);
    assert(aShellDeInit() == A_STATUS_NOT_READY);
    assert(!allocations && !mutexes);
    return 0;
}
