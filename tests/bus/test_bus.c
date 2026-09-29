#include "aBus.h"
#include "aOS.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

/* Only the bus object receives NDEBUG; test assertions always execute. */
#if ABUS_TEST_NDEBUG
#define EXPECT_ASSERT(expression) ((void)0)
#else
#define EXPECT_ASSERT(expression) do { \
    pid_t child = fork(); \
    int result; \
    assert(child >= 0); \
    if (child == 0) { \
        struct rlimit limit = {0, 0}; \
        (void)setrlimit(RLIMIT_CORE, &limit); \
        if (freopen("/dev/null", "w", stderr) == NULL) _exit(2); \
        (void)(expression); \
        _exit(0); \
    } \
    assert(waitpid(child, &result, 0) == child); \
    assert(WIFSIGNALED(result) && WTERMSIG(result) == SIGABRT); \
} while (0)
#endif

static int live_locks;
static int live_allocations;
static int create_budget = -1;
static int fail_alloc;
static int fail_lock;
static aOSMutex_t created_locks[4];

void *aOSAlloc(size_t size)
{
    void *result;
    if (fail_alloc) return NULL;
    result = malloc(size);
    if (result != NULL) ++live_allocations;
    return result;
}
void aOSFree(void *memory)
{
    if (memory != NULL) --live_allocations;
    free(memory);
}
aStatus_t aOSMutexCreate(aOSMutex_t *mutex)
{
    pthread_mutex_t *value;
    if (*mutex != NULL) return A_STATUS_INVALID_PARAM;
    if (fail_alloc || create_budget == 0) return A_STATUS_NO_MEMORY;
    if (create_budget > 0) --create_budget;
    value = malloc(sizeof(*value));
    assert(value != NULL);
    assert(pthread_mutex_init(value, NULL) == 0);
    *mutex = value;
    created_locks[live_locks++] = value;
    return A_STATUS_OK;
}
void aOSMutexDestroy(aOSMutex_t *mutex)
{
    if (*mutex == NULL) return;
    assert(pthread_mutex_destroy(*mutex) == 0);
    free(*mutex);
    *mutex = NULL;
    --live_locks;
}
aStatus_t aOSMutexLock(aOSMutex_t mutex, aTimeout_t timeout)
{
    int result;

    if (fail_lock) return A_STATUS_TIMEOUT;
    if (timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
        timeout.milliseconds == 0U) {
        result = pthread_mutex_trylock(mutex);
        if (result == EBUSY) return A_STATUS_BUSY;
        assert(result == 0);
        return A_STATUS_OK;
    }
    assert(pthread_mutex_lock(mutex) == 0);
    return A_STATUS_OK;
}
aStatus_t aOSMutexUnlock(aOSMutex_t mutex)
{
    assert(pthread_mutex_unlock(mutex) == 0);
    return A_STATUS_OK;
}

typedef struct { uint32_t a; uint32_t b; } Pair;
static Pair current = {1U, 1U};
static uint32_t secondary;
static uint32_t plain;
static aBus_SigState states[] = {
    {.data = &current}, {.data = &secondary}, {.data = &plain}
};
static const aBus_ItemDef items[] = {
    {.offset = offsetof(Pair, a), .type = ALIB_SCALAR_U32,
     .min.u32 = 0, .max.u32 = UINT32_MAX, .default_value.u32 = 1},
    {.offset = offsetof(Pair, b), .type = ALIB_SCALAR_U32,
     .min.u32 = 0, .max.u32 = UINT32_MAX, .default_value.u32 = 1}
};
static const aBus_SigDef defs[] = {
    {.sigID = 10, .size = sizeof(Pair), .flags = ABUS_SIG_FLAG_LOCK,
     .items = items, .itemCount = 2, .state = &states[0]},
    {.sigID = 20, .size = sizeof(uint32_t), .flags = ABUS_SIG_FLAG_LOCK,
     .state = &states[1]},
    {.sigID = 30, .size = sizeof(uint32_t), .state = &states[2]}
};

static void *writer(void *arg)
{
    (void)arg;
    for (uint32_t i = 0; i < 20000; ++i) {
        Pair pair = {i, i};
        assert(aBusSetSig(10, &pair, sizeof(pair), A_TIMEOUT_FOREVER) == 0);
    }
    return NULL;
}
static void *reader(void *arg)
{
    (void)arg;
    for (unsigned i = 0; i < 20000; ++i) {
        Pair pair;
        assert(aBusGetSig(10, &pair, sizeof(pair), A_TIMEOUT_FOREVER) == 0);
        assert(pair.a == pair.b);
    }
    return NULL;
}

static void validation_tests(void)
{
    unsigned char storage[16] = {0};
    unsigned char input[16] = {0};
    aBus_SigState state = {.data = storage};
    aBus_ItemDef rules[] = {
        {.offset = 0, .type = ALIB_SCALAR_U8,
         .min.u8 = 0, .max.u8 = UINT8_MAX,
         .default_value.u8 = 42},
        {.offset = 1, .type = ALIB_SCALAR_U16,
         .min.u16 = 0, .max.u16 = UINT16_MAX,
         .default_value.u16 = UINT16_MAX},
        {.offset = 3, .type = ALIB_SCALAR_U32,
         .min.u32 = 0, .max.u32 = UINT32_MAX,
         .default_value.u32 = UINT32_MAX},
        {.offset = 7, .type = ALIB_SCALAR_I32,
         .min.i32 = -100, .max.i32 = 100,
         .default_value.i32 = -100}
    };
    aBus_SigDef def = {
        .sigID = 1, .size = sizeof(storage), .items = rules,
        .itemCount = 4, .state = &state
    };
    aBusConfig_t config = {&def, 1};
    uint16_t u16 = UINT16_MAX;
    uint32_t u32 = UINT32_MAX;
    int32_t signed_value = -100;

    assert(aScalarSize(ALIB_SCALAR_U8) == sizeof(uint8_t));
    assert(aScalarSize(ALIB_SCALAR_U16) == sizeof(uint16_t));
    assert(aScalarSize(ALIB_SCALAR_U32) == sizeof(uint32_t));
    assert(aScalarSize(ALIB_SCALAR_I32) == sizeof(int32_t));
    assert(aScalarSize((aScalarType_t)-1) == 0U);
    assert(aScalarSize((aScalarType_t)99) == 0U);
    fail_alloc = ABUS_LOCK_MODE == 0;
    storage[15] = 55;
    input[0] = UINT8_MAX;
    input[15] = 77;
    memcpy(input + 1, &u16, sizeof(u16));
    memcpy(input + 3, &u32, sizeof(u32));
    memcpy(input + 7, &signed_value, sizeof(signed_value));
    assert(aBusInit(&config) == 0);
    assert(live_locks == (ABUS_LOCK_MODE == 0 ? 0 : 1));
    fail_alloc = 0;
    assert(storage[0] == 42 && storage[15] == 55);
    memcpy(&u16, storage + 1, sizeof(u16));
    memcpy(&u32, storage + 3, sizeof(u32));
    memcpy(&signed_value, storage + 7, sizeof(signed_value));
    assert(u16 == UINT16_MAX && u32 == UINT32_MAX);
    assert(signed_value == -100);
    assert(aBusSetSig(1, input, sizeof(input), A_TIMEOUT_NO_WAIT) == 0);
    assert(memcmp(storage, input, sizeof(input)) == 0);
    signed_value = -101;
    memcpy(input + 7, &signed_value, sizeof(signed_value));
    assert(aBusSetSig(1, input, sizeof(input), A_TIMEOUT_NO_WAIT) ==
           A_STATUS_INVALID_PARAM);
    memcpy(&signed_value, storage + 7, sizeof(signed_value));
    assert(signed_value == -100);
    aBusDeInit();
#if ABUS_DEF_CHECK_ENABLE
    rules[0].offset = 16;
    assert(aBusInit(&config) == A_STATUS_INVALID_PARAM);
    rules[0].offset = 0;
    rules[0].min.u8 = 100;
    rules[0].max.u8 = 10;
    assert(aBusInit(&config) == A_STATUS_INVALID_PARAM);
    rules[0].min.u8 = 0;
    rules[0].max.u8 = 41;
    assert(aBusInit(&config) == A_STATUS_INVALID_PARAM);
    assert(storage[0] == UINT8_MAX);
    rules[0].max.u8 = UINT8_MAX;
    rules[0].type = (aScalarType_t)99;
    assert(aBusInit(&config) == A_STATUS_INVALID_PARAM);
    rules[0].type = ALIB_SCALAR_U8;
    rules[1].offset = 0;
    assert(aBusInit(&config) == A_STATUS_INVALID_PARAM);
    rules[1].offset = 1;
#endif
    rules[3].min.i32 = INT32_MIN;
    rules[3].max.i32 = INT32_MAX;
    rules[3].default_value.i32 = INT32_MIN;
    assert(aBusInit(&config) == 0);
    memcpy(&signed_value, storage + 7, sizeof(signed_value));
    assert(signed_value == INT32_MIN);
    aBusDeInit();
}

int main(void)
{
    aBusConfig_t config;
    Pair output = {99, 99};
#if ABUS_LOCK_MODE != 0
    pthread_t threads[4];
#endif
#if ABUS_DEF_CHECK_ENABLE
    aBus_SigDef bad[2];
#endif

    aBusConfigStructInit(&config);
    EXPECT_ASSERT(aBusInit(&config));
    EXPECT_ASSERT(aBusInit(NULL));
    config.signals = defs;
    config.signal_count = 3;
    assert(aBusGetSig(10, &output, sizeof(output), A_TIMEOUT_NO_WAIT) ==
           A_STATUS_NOT_READY);
    current.a = 77;
    current.b = 77;
#if ABUS_LOCK_MODE != 0
    fail_alloc = 1;
    assert(aBusInit(&config) == A_STATUS_NO_MEMORY);
    fail_alloc = 0;
    create_budget = 0;
    assert(aBusInit(&config) == A_STATUS_NO_MEMORY);
    assert(live_locks == 0 && live_allocations == 0);
#if ABUS_LOCK_MODE == 2
    create_budget = 1;
    assert(aBusInit(&config) == A_STATUS_NO_MEMORY);
    assert(live_locks == 0 && live_allocations == 0);
#endif
    assert(current.a == 77 && current.b == 77);
#if ABUS_LOCK_MODE == ABUS_LOCK_SIG
    assert(states[0].mutex == NULL && states[1].mutex == NULL);
    assert(states[2].mutex == NULL);
#endif
    create_budget = -1;
#else
    /* NONE 模式不得调用分配或锁创建，即使组标志要求使用锁。 */
    fail_alloc = 1;
    create_budget = 0;
#endif
    assert(aBusInit(&config) == 0);
    fail_alloc = 0;
    create_budget = -1;
    assert(live_locks == (ABUS_LOCK_MODE == 2 ? 3 : ABUS_LOCK_MODE));
    assert(live_allocations == 0);
#if ABUS_LOCK_MODE == ABUS_LOCK_SIG
    assert(states[0].mutex == created_locks[0]);
    assert(states[1].mutex == created_locks[1]);
    assert(states[2].mutex == created_locks[2]);
#else
    _Static_assert(sizeof(aBus_SigState) == sizeof(void *),
                   "NONE/BUS state must not contain a mutex");
#endif
    assert(aBusInit(&config) == A_STATUS_BUSY);
    assert(aBusGetSig(10, &output, sizeof(output), A_TIMEOUT_NO_WAIT) == 0);
    assert(output.a == 1 && output.b == 1);
    EXPECT_ASSERT(aBusGetSig(10, NULL, sizeof(output), A_TIMEOUT_NO_WAIT));
    EXPECT_ASSERT(aBusGetSig(10, &current, sizeof(current), A_TIMEOUT_NO_WAIT));
    EXPECT_ASSERT(aBusSetSig(10, &current, sizeof(current), A_TIMEOUT_NO_WAIT));
    EXPECT_ASSERT(aBusGetSig(10, (unsigned char *)&current + 1, sizeof(current),
                       A_TIMEOUT_NO_WAIT));
    EXPECT_ASSERT(aBusSetSig(10, (unsigned char *)&current + 1, sizeof(current),
                       A_TIMEOUT_NO_WAIT));
    {
        aTimeout_t invalid = {.type = (aTimeoutType_t)99};
        (void)invalid;
        EXPECT_ASSERT(aBusGetSig(10, &output, sizeof(output), invalid));
    }
#if ABUS_LOCK_MODE != 0
    assert(pthread_mutex_lock(created_locks[0]) == 0);
    assert(aBusGetSig(10, &output, sizeof(output), A_TIMEOUT_NO_WAIT) ==
           A_STATUS_BUSY);
    assert(aBusSetSig(10, &output, sizeof(output), A_TIMEOUT_NO_WAIT) ==
           A_STATUS_BUSY);
    assert(aBusGetSig(20, &output.a, sizeof(output.a), A_TIMEOUT_NO_WAIT) ==
           (ABUS_LOCK_MODE == 2 ? A_STATUS_OK : A_STATUS_BUSY));
    assert(pthread_mutex_unlock(created_locks[0]) == 0);
#endif
    assert(aBusGetSig(99, &output, sizeof(output), A_TIMEOUT_NO_WAIT) ==
           A_STATUS_NOT_FOUND);
    EXPECT_ASSERT(aBusSetSig(10, &output, 1, A_TIMEOUT_NO_WAIT));
    EXPECT_ASSERT(aBusSetSig(10, NULL, sizeof(output), A_TIMEOUT_NO_WAIT));
#if ABUS_LOCK_MODE != 0
    fail_lock = 1;
    output.a = 99;
    assert(aBusGetSig(10, &output, sizeof(output), A_TIMEOUT_NO_WAIT) ==
           A_STATUS_TIMEOUT);
    assert(output.a == 99);
    assert(aBusSetSig(10, &output, sizeof(output), A_TIMEOUT_NO_WAIT) ==
           A_STATUS_TIMEOUT);
    assert(current.a == 1);
#endif
    assert(aBusSetSig(30, &output.a, sizeof(output.a),
                       A_TIMEOUT_NO_WAIT) == 0);
    fail_lock = 0;
#if ABUS_LOCK_MODE != 0
    for (unsigned i = 0; i < 4; ++i) {
        assert(pthread_create(&threads[i], NULL,
                             i < 2 ? writer : reader, NULL) == 0);
    }
    for (unsigned i = 0; i < 4; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
#else
    /* 无锁模式仅串行测试，不制造普通内存的并发数据竞争。 */
    writer(NULL);
    reader(NULL);
#endif
    aBusDeInit();
#if ABUS_LOCK_MODE == ABUS_LOCK_SIG
    assert(states[0].mutex == NULL);
    assert(states[1].mutex == NULL);
    assert(states[2].mutex == NULL);
#endif
    aBusDeInit();
    assert(live_locks == 0 && live_allocations == 0);
#if ABUS_DEF_CHECK_ENABLE
    bad[0] = defs[0];
    bad[1] = defs[1];
    config.signals = bad;
    config.signal_count = 2;
    bad[1].state = bad[0].state;
    assert(aBusInit(&config) == A_STATUS_INVALID_PARAM);
    bad[1] = defs[1];
    bad[1].flags = 2U;
    assert(aBusInit(&config) == A_STATUS_INVALID_PARAM);
    bad[1] = defs[1];
    bad[1].state = NULL;
    assert(aBusInit(&config) == A_STATUS_INVALID_PARAM);
    bad[1] = defs[1];
    bad[1].size = 0;
    assert(aBusInit(&config) == A_STATUS_INVALID_PARAM);
    bad[1] = defs[1];
    bad[1].itemCount = 1;
    assert(aBusInit(&config) == A_STATUS_INVALID_PARAM);
#endif
    validation_tests();
    assert(live_locks == 0 && live_allocations == 0);
    return 0;
}
