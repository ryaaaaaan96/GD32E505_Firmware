#include "aShell.h"
#include "aShell_config.h"
#include "aOS.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sched.h>
#include <stdatomic.h>

static unsigned allocations, mutexes, calls;
static _Thread_local unsigned lock_depth;
static pthread_mutex_t queue_mutex;
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
aStatus_t aOSMutexCreate(aOSMutex_t *p)
{
    if (fail_mutex) return A_STATUS_NO_MEMORY;
    assert(pthread_mutex_init(&queue_mutex, NULL) == 0);
    ++mutexes;
    *p = &queue_mutex;
    return A_STATUS_OK;
}
void aOSMutexDestroy(aOSMutex_t *p)
{
    if (*p) {
        assert(pthread_mutex_destroy(*p) == 0);
        --mutexes;
        *p = NULL;
    }
}
aStatus_t aOSMutexLock(aOSMutex_t p, aTimeout_t timeout)
{
    int status;

    assert(p && !lock_depth);
    status = timeout.type == A_TIMEOUT_TYPE_FOREVER ?
        pthread_mutex_lock(p) : pthread_mutex_trylock(p);
    if (status == EBUSY) return A_STATUS_BUSY;
    assert(status == 0);
    ++lock_depth;
    return A_STATUS_OK;
}
aStatus_t aOSMutexUnlock(aOSMutex_t p)
{
    assert(p && lock_depth == 1U);
    --lock_depth;
    assert(pthread_mutex_unlock(p) == 0);
    return A_STATUS_OK;
}

static aSSize_t read_input(void *p, size_t n, aTimeout_t timeout)
{
    size_t count;
    assert(n == 64 && lock_depth == 0U);
    assert(timeout.milliseconds == 20U);
    if (forced_read != -99) return forced_read;
    count = input ? strlen(input) : 0U;
    if (count > n) count = n;
    if (count) { memcpy(p, input, count); input += count; }
    return (aSSize_t)count;
}
static aSSize_t write_output(const void *p, size_t n, aTimeout_t timeout)
{
    assert(write_calls < 65536 && lock_depth == 0U);
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
    assert(lock_depth == 0U);
    ++calls;
    last_argc = argc;
    strcpy(last_arg, argv[argc - 1]);
    assert(ASHELL_PRINT("captured\r\n") == A_STATUS_OK);
    return 0;
}
ASHELL_CMD_EXPORT(capture, capture, "Record arguments");
ASHELL_CMD_EXPORT(cat, capture, "Alias");
ASHELL_CMD_EXPORT(cancel, capture, "Third ambiguous completion");

static int bulk_reply(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (unsigned i = 0U; i < 160U; i++) {
        if (ASHELL_REPLY("reply %03u: abcdefghijklmnopqrstuvwxyz\r\n", i)
            != A_STATUS_OK) return -1;
    }
    return 0;
}
ASHELL_CMD_EXPORT(bulk, bulk_reply, "Exercise replies larger than the queue");

static void test_bulk_reply(void)
{
    aShellOutputStats_t before, after;
    assert(aShellGetOutputStats(&before) == A_STATUS_OK);
    reset_output();
    feed("bulk\r");
    assert(strstr(output, "reply 000:") != NULL);
    assert(strstr(output, "reply 159:") != NULL);
    assert(aShellGetOutputStats(&after) == A_STATUS_OK);
    assert(after.dropped_messages == before.dropped_messages);
    assert(write_calls > 1U);
}

static void test_reply_failure(void)
{
    aShellOutputStats_t before, after;
    assert(aShellGetOutputStats(&before) == A_STATUS_OK);
    reset_output();
    write_mode = 2U;
    input = "bulk\r";
    assert(aShellProcess() == A_STATUS_ERROR);
    assert(aShellGetOutputStats(&after) == A_STATUS_OK);
    assert(after.dropped_messages == before.dropped_messages + 1U);
    write_mode = 0U;
    assert(aShellProcess() == A_STATUS_BUSY);
    assert(strstr(output, "reply interrupted; retry command.") != NULL);
    reset_output();
    feed("bulk\r");
    assert(strstr(output, "reply 159:") != NULL);
}

static void test_queue(void)
{
    aShellOutputStats_t stats;
    char block[ASHELL_PRINT_BUFFER_SIZE];
    char too_long[ASHELL_PRINT_BUFFER_SIZE + 1U];
    unsigned count = 0U;
    unsigned before;

    reset_output();
    memset(block, 'q', sizeof(block) - 1U);
    block[sizeof(block) - 1U] = '\0';
    while (ASHELL_PRINT("%s", block) == A_STATUS_OK) ++count;
    assert(count == ASHELL_OUTPUT_BUFFER_SIZE / strlen(block));
    assert(write_calls == 0U);
    assert(aShellGetOutputStats(&stats) == A_STATUS_OK);
    assert(stats.pending_bytes == count * strlen(block));
    assert(stats.dropped_messages == 1U);
    assert(aShellProcess() == A_STATUS_BUSY);
    assert(output_size == count * strlen(block));
    for (size_t i = 0; i < output_size; ++i) assert(output[i] == 'q');

    memset(too_long, 'x', sizeof(too_long) - 1U);
    too_long[sizeof(too_long) - 1U] = '\0';
    assert(ASHELL_PRINT("%s", too_long) == A_STATUS_INVALID_PARAM);
    assert(ASHELL_PRINT(NULL) == A_STATUS_INVALID_PARAM);
    assert(aShellGetOutputStats(&stats) == A_STATUS_OK);
    assert(stats.pending_bytes == 0U && stats.dropped_messages == 2U);

    /* Lock contention must return immediately and count exactly one drop. */
    assert(pthread_mutex_lock(&queue_mutex) == 0);
    assert(ASHELL_PRINT("busy") == A_STATUS_BUSY);
    assert(pthread_mutex_unlock(&queue_mutex) == 0);
    assert(aShellGetOutputStats(&stats) == A_STATUS_OK);
    assert(stats.dropped_messages == 3U);

    /* A long synchronous command fills its own queue without deadlocking. */
    before = calls;
    feed("burst\n");
    assert(calls == before + 1U);
    assert(aShellGetOutputStats(&stats) == A_STATUS_OK);
    assert(stats.dropped_messages > 3U && stats.pending_bytes == 0U);
}

static int burst_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    assert(lock_depth == 0U);
    ++calls;
    for (unsigned i = 0U; i < 200U; ++i) {
        (void)ASHELL_PRINT("burst output\r\n");
    }
    return 0;
}
ASHELL_CMD_EXPORT(burst, burst_command, "Fill output queue");

#define PRODUCERS 4U
#define MESSAGES 200U
static aBool_t accepted[PRODUCERS][MESSAGES];
static atomic_uint finished;

static void *producer(void *argument)
{
    size_t id = (size_t)argument;
    aStatus_t status;

    for (unsigned i = 0U; i < MESSAGES; ++i) {
        status = ASHELL_PRINT("<%02u:%03u>", (unsigned)id, i);
        assert(status == A_STATUS_OK || status == A_STATUS_BUSY);
        accepted[id][i] = status == A_STATUS_OK;
        sched_yield();
    }
    (void)atomic_fetch_add(&finished, 1U);
    return NULL;
}

static void test_producers(void)
{
    pthread_t threads[PRODUCERS];
    aShellOutputStats_t stats;
    aStatus_t status;
    unsigned before;
    unsigned total = 0U;
    unsigned id;
    unsigned sequence;
    char message[9];

    reset_output();
    write_limit = 3U;
    assert(aShellGetOutputStats(&stats) == A_STATUS_OK);
    before = stats.dropped_messages;
    atomic_init(&finished, 0U);
    for (size_t i = 0U; i < PRODUCERS; ++i) {
        assert(pthread_create(&threads[i], NULL, producer, (void *)i) == 0);
    }
    do {
        status = aShellProcess();
        assert(status == A_STATUS_BUSY || status == A_STATUS_TIMEOUT);
    } while (atomic_load(&finished) != PRODUCERS);
    for (unsigned i = 0U; i < PRODUCERS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        for (unsigned j = 0U; j < MESSAGES; ++j) total += accepted[i][j];
    }
    do {
        status = aShellProcess();
        assert(status == A_STATUS_BUSY || status == A_STATUS_TIMEOUT);
        assert(aShellGetOutputStats(&stats) == A_STATUS_OK);
    } while (stats.pending_bytes != 0U);
    assert(output_size == total * 8U);
    assert(stats.dropped_messages - before == PRODUCERS * MESSAGES - total);
    for (size_t i = 0U; i < output_size; i += 8U) {
        memcpy(message, output + i, 8U);
        message[8] = '\0';
        assert(message[0] == '<' && message[7] == '>');
        assert(sscanf(message, "<%2u:%3u>", &id, &sequence) == 2);
        assert(id < PRODUCERS && sequence < MESSAGES);
        assert(accepted[id][sequence]);
        accepted[id][sequence] = A_FALSE; /* No duplication/interleaving. */
    }
}

int main(void)
{
    aShellConfig_t config;
    aShellOutputStats_t stats;
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
    config.read_timeout = A_TIMEOUT_FOREVER;
    assert(aShellInit(&config) == A_STATUS_INVALID_PARAM);
    config.read_timeout = A_TIMEOUT_MS(20U);
    assert(aShellInit(&config) == A_STATUS_OK);
    assert(write_calls == 0U); /* Welcome and prompt are only queued. */
    assert(aShellInit(&config) == A_STATUS_BUSY);
    assert(aShellProcess() == A_STATUS_BUSY);
    assert(strncmp(output, ASHELL_WELCOME,
                   sizeof(ASHELL_WELCOME) - 1U) == 0);
    assert(strstr(output + sizeof(ASHELL_WELCOME) - 1U,
                  ASHELL_PROMPT ": "));
    reset_output();
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
    feed("\r"); /* I/O 故障后先恢复行边界。 */
    before = calls;
    feed("capture dam");
    forced_read = -1;
    last_error = A_EIO;
    assert(aShellProcess() == A_STATUS_ERROR);
    forced_read = -99;
    feed("aged\r");
    assert(calls == before); /* 出错半行不能执行。 */
    feed("capture recovered\r");
    assert(calls == before + 1U && !strcmp(last_arg, "recovered"));

    reset_output();
    write_limit = 2;
    strcpy(line, "abcdef");
    assert(ASHELL_PRINT("%s", line) == A_STATUS_OK);
    strcpy(line, "changed");
    assert(write_calls == 0); /* Copied, never writes from producer. */
    assert(aShellProcess() == A_STATUS_BUSY);
    assert(!strcmp(output, "abcdef"));
    assert(write_calls == 3 && budgets[0] == 20 && budgets[1] == 16);
    for (i = 1; i <= 5; ++i) {
        reset_output();
        write_limit = 2;
        write_mode = i;
        assert(ASHELL_PRINT("abcdef") == A_STATUS_OK);
        assert(write_calls == 0);
        assert(aShellProcess() != A_STATUS_OK);
        assert(write_calls == (i == 5 ? 2U : 1U));
        write_mode = 0;
        assert(aShellProcess() == A_STATUS_BUSY);
        assert(!strcmp(output, "abcdef")); /* No replay of accepted prefix. */
    }
    test_bulk_reply();
    test_queue();
    test_producers();
    test_reply_failure();
    assert(aShellDeInit() == A_STATUS_OK);
    assert(!allocations && !mutexes && !lock_depth);
    assert(ASHELL_PRINT("not ready") == A_STATUS_NOT_READY);
    assert(aShellGetOutputStats(&stats) == A_STATUS_NOT_READY);

    config.write_timeout = A_TIMEOUT_MS(5U);
    assert(aShellInit(&config) == A_STATUS_OK);
    reset_output();
    assert(aShellProcess() == A_STATUS_BUSY);
    reset_output();
    write_limit = 2;
    assert(ASHELL_PRINT("abcdef") == A_STATUS_OK);
    assert(aShellProcess() == A_STATUS_TIMEOUT);
    assert(output_size == 4 && write_calls == 2);
    assert(aShellGetOutputStats(&stats) == A_STATUS_OK);
    assert(stats.pending_bytes == 2);
    assert(aShellProcess() == A_STATUS_BUSY);
    assert(!strcmp(output, "abcdef"));
    reset_output();
    feed("capture fresh\n");
    assert(!strcmp(last_arg, "fresh"));
    assert(aShellDeInit() == A_STATUS_OK);
    config.write_timeout = A_TIMEOUT_NO_WAIT;
    assert(aShellInit(&config) == A_STATUS_OK);
    reset_output();
    assert(aShellProcess() == A_STATUS_BUSY);
    reset_output();
    write_limit = 2;
    assert(ASHELL_PRINT("abcdef") == A_STATUS_OK);
    assert(aShellProcess() == A_STATUS_TIMEOUT);
    assert(output_size == 2 && write_calls == 1);
    aStreamStructInit(&config.stream);
    reset_output();
    assert(aShellProcess() == A_STATUS_BUSY);
    assert(!strcmp(output, "cdef"));
    assert(ASHELL_PRINT("discard on deinit") == A_STATUS_OK);
    assert(aShellDeInit() == A_STATUS_OK);
    assert(aShellDeInit() == A_STATUS_NOT_READY);
    assert(!allocations && !mutexes);
    return 0;
}
