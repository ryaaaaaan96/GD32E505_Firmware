#include "aBus_instance.h"
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

static aBusHandle_t *initializing_handle;
static int live_locks;
static int live_allocations;
static int create_budget = -1;
static int fail_alloc;
static int alloc_budget = -1;
static int fail_lock;
static aOSMutex_t created_locks[16];

void *aOSAlloc(size_t size)
{
    void *result;
    if (fail_alloc || alloc_budget == 0) return NULL;
    if (alloc_budget > 0) alloc_budget--;
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
    if (initializing_handle != NULL) {
        assert(initializing_handle->table == NULL);
    }
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

static aStatus_t set_sig(aBusHandle_t *handle, uint16_t id,
                         const void *src, size_t size, aTimeout_t timeout)
{
    aBusSetKeyRequest_t request;

    aBusSetKeyRequestStructInit(&request);
    request.sigKey = id;
    request.src = src;
    request.size = size;
    request.timeout = timeout;
    return aBusSetByKey(handle, &request);
}

static aStatus_t get_sig(aBusHandle_t *handle, uint16_t id,
                         void *dst, size_t size, aTimeout_t timeout)
{
    aBusGetKeyRequest_t request;

    aBusGetKeyRequestStructInit(&request);
    request.sigKey = id;
    request.dst = dst;
    request.size = size;
    request.timeout = timeout;
    return aBusGetByKey(handle, &request);
}

typedef struct { uint32_t a; uint32_t b; uint32_t tag; } Pair;
static const Pair pair_default = {1, 1, 0};
static const aBusParam_t fields[] = {
    {.offset = offsetof(Pair, a),
     .type = ALIB_DATA_U32,
     .min.u32 = 0, .max.u32 = 10000},
    {.offset = offsetof(Pair, b),
     .type = ALIB_DATA_U32,
     .min.u32 = 0, .max.u32 = UINT32_MAX}
};
static const aBusSig_t defs[] = {
    {.sigKey = 10, .size = sizeof(Pair), .default_data = &pair_default,
     .flags = ABUS_SIG_FLAG_LOCK,
     .params = fields, .param_count = 2},
    {.sigKey = 20, .size = sizeof(Pair), .default_data = &pair_default,
     .params = fields, .param_count = 2}
};

/* 绑定对象在另一个翻译单元，验证 static 变量无需 extern 暴露。 */
const aBusTable_t bound_table = {
    .sigs = defs, .sig_count = 2, .deviceID = 1
};
const aBusTable_t mixed_table = {
    .sigs = defs, .sig_count = 2, .deviceID = 2
};
extern void bindings_fill(void);
extern int bindings_unchanged(void);
extern void *bindings_first(void);

static void *writer(void *arg)
{
    aBusHandle_t *handle = arg;
    for (uint32_t i = 0; i < 2000; i++) {
        Pair input = {i, i, 0};
        assert(set_sig(handle, 10, &input, sizeof(input),
                          A_TIMEOUT_FOREVER) == A_STATUS_OK);
    }
    return NULL;
}
static void *reader(void *arg)
{
    aBusHandle_t *handle = arg;
    for (unsigned i = 0; i < 2000; i++) {
        Pair output;
        assert(get_sig(handle, 10, &output, sizeof(output),
                          A_TIMEOUT_FOREVER) == A_STATUS_OK);
        assert(output.a == output.b);
    }
    return NULL;
}

static void io_tests(aBusHandle_t *handle)
{
    Pair input = {10001, 1, 0};
    Pair output;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    pthread_t threads[4];
#endif

    assert(set_sig(handle, 10, &input, sizeof(input),
                      A_TIMEOUT_NO_WAIT) == A_STATUS_INVALID_PARAM);
    assert(get_sig(handle, 10, &output, sizeof(output),
                      A_TIMEOUT_NO_WAIT) == A_STATUS_OK);
    assert(output.a == 1 && output.b == 1);
    assert(get_sig(handle, 99, &output, sizeof(output),
                      A_TIMEOUT_NO_WAIT) == A_STATUS_NOT_FOUND);
    EXPECT_ASSERT(get_sig(handle, 10, NULL, sizeof(output),
                             A_TIMEOUT_NO_WAIT));
    EXPECT_ASSERT(set_sig(handle, 10, &input, 1, A_TIMEOUT_NO_WAIT));
    EXPECT_ASSERT(get_sig(handle, 10, handle->sigs[0].data,
                             sizeof(output), A_TIMEOUT_NO_WAIT));
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    fail_lock = 1;
    output.a = 77;
    assert(get_sig(handle, 10, &output, sizeof(output),
                      A_TIMEOUT_NO_WAIT) == A_STATUS_TIMEOUT);
    assert(output.a == 77);
    /* 标志不启用的条目也创建了锁，但读写时不使用。 */
    assert(get_sig(handle, 20, &output, sizeof(output),
                      A_TIMEOUT_NO_WAIT) == A_STATUS_OK);
    fail_lock = 0;
    for (unsigned i = 0; i < 4; i++) {
        assert(pthread_create(&threads[i], NULL,
                              i < 2 ? writer : reader, handle) == 0);
    }
    for (unsigned i = 0; i < 4; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
#else
    writer(handle);
    reader(handle);
#endif
}

#if ABUS_STATIC_ENABLE
static void static_tests(void)
{
    aBusHandle_t instance;
    aBusHandle_t *handle = NULL;
    aBusSigState_t storage[2];
    Pair output;
    const aBusTable_t *table = &bound_table;

    bindings_fill();

    aBusInstanceStructInit(&instance, storage, 1);
    assert(aBusInitStatic(table, &instance) ==
           A_STATUS_INVALID_PARAM);
    assert(instance.table == NULL && bindings_unchanged());
    aBusInstanceStructInit(&instance, storage, 2);
    initializing_handle = &instance;
    assert(get_sig(&instance, 10, &output, sizeof(output),
                      A_TIMEOUT_NO_WAIT) == A_STATUS_NOT_READY);
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    for (int budget = 0; budget < (ABUS_LOCK_MODE == 2 ? 2 : 1); budget++) {
        create_budget = budget;
        assert(aBusInitStatic(table, &instance) ==
               A_STATUS_NO_MEMORY);
        assert(instance.table == NULL);
        assert(live_locks == 0 && bindings_unchanged());
    }
    create_budget = -1;
#endif
    assert(aBusInitStatic(table, &instance) == A_STATUS_OK);
    initializing_handle = NULL;
    handle = &instance;
    assert(handle->table == table && live_allocations == 0);
    assert(get_sig(handle, 10, &output, sizeof(output),
                      A_TIMEOUT_NO_WAIT) == A_STATUS_OK);
    assert(output.a == 1 && output.tag == 0);
    assert(aBusInitStatic(table, &instance) == A_STATUS_BUSY);
    assert(instance.table == table);
#if ABUS_DYNAMIC_ENABLE
    assert(aBusDestroy(handle) == A_STATUS_INVALID_PARAM);
#endif
    io_tests(handle);
    assert(aBusDeInitStatic(handle) == A_STATUS_OK);
    assert(aBusDeInitStatic(handle) == A_STATUS_OK);
    assert(live_locks == 0 && live_allocations == 0);

    assert(instance.table == NULL);
    assert(get_sig(handle, 10, &output, sizeof(output),
                      A_TIMEOUT_NO_WAIT) == A_STATUS_NOT_READY);
    assert(set_sig(handle, 10, &output, sizeof(output),
                      A_TIMEOUT_NO_WAIT) == A_STATUS_NOT_READY);
    /* 反初始化保留静态存储信息，可直接重试而无需重建实例。 */
    initializing_handle = &instance;
    assert(aBusInitStatic(table, &instance) == A_STATUS_OK);
    initializing_handle = NULL;
    assert(aBusDeInitStatic(handle) == A_STATUS_OK);

    assert(aBusInitStatic(&mixed_table, &instance) == A_STATUS_NOT_FOUND);
    assert(live_allocations == 0 && live_locks == 0);
}
#endif

#if ABUS_DYNAMIC_ENABLE
static void dynamic_tests(void)
{
    Pair output;
    aBusHandle_t *first = NULL;
    aBusHandle_t *second = NULL;
    aBusTable_t table = {.sigs = defs, .sig_count = 2, .deviceID = 1};
    aBusTable_t second_table;

    /* handle、条目数组、动态数据每一处分配失败都须完整回收。 */
    for (int budget = 0; budget < 2; budget++) {
        alloc_budget = budget;
        assert(aBusCreate(&table, &first) == A_STATUS_NO_MEMORY);
        assert(first == NULL && live_allocations == 0 && live_locks == 0);
    }
    alloc_budget = -1;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    for (int budget = 0; budget < (ABUS_LOCK_MODE == 2 ? 2 : 1); budget++) {
        create_budget = budget;
        assert(aBusCreate(&table, &first) == A_STATUS_NO_MEMORY);
        assert(first == NULL && live_allocations == 0 && live_locks == 0);
    }
    create_budget = -1;
#endif
    assert(aBusCreate(&table, &first) == A_STATUS_OK);
    assert(live_allocations == 2);
#if ABUS_STATIC_ENABLE
    assert(aBusDeInitStatic(first) == A_STATUS_INVALID_PARAM);
#endif
    io_tests(first);
    second_table = table;
    second_table.deviceID = 2;
    assert(aBusCreate(&second_table, &second) == A_STATUS_OK);
    assert(first->table->deviceID == 1 && second->table->deviceID == 2);
    assert(get_sig(second, 10, &output, sizeof(output),
                      A_TIMEOUT_NO_WAIT) == A_STATUS_OK);
    assert(output.a == 1 && output.tag == 0);
    assert(aBusDestroy(first) == A_STATUS_OK);
    assert(get_sig(second, 10, &output, sizeof(output),
                      A_TIMEOUT_NO_WAIT) == A_STATUS_OK);
    assert(aBusDestroy(second) == A_STATUS_OK);
    assert(aBusDestroy(NULL) == A_STATUS_OK);
    assert(live_allocations == 0 && live_locks == 0);
}
#endif

/* 无描述的任意字节与部分整数规则共存，检查非对齐字段。 */
static void model_tests(void)
{
    static unsigned char defaults[16] = {42, 0, 0, 0, 0, 0, 0, 0xA5};
    unsigned char output[16];
    unsigned char input[16];
    int32_t signed_value = -100;
    uint16_t unsigned_value = UINT16_MAX;
    static aBusParam_t params[] = {
        {.offset = 0, .type = ALIB_DATA_U8, .max.u8 = 100},
        {.offset = 1, .type = ALIB_DATA_U16, .max.u16 = UINT16_MAX},
        {.offset = 3, .type = ALIB_DATA_S32,
         .min.s32 = -100, .max.s32 = 100}
    };
    static aBusSig_t sigs[] = {
        {.sigKey = 1, .size = 1},
        {.sigKey = 2, .size = sizeof(defaults), .default_data = defaults,
         .params = params, .param_count = 3}
    };
    static aBusTable_t table = {.sigs = sigs, .sig_count = 2};
    static unsigned char byte_storage;
    static unsigned char group_storage[16];
    ABUS_STORAGE_EXPORT(model_byte, table, 0, byte_storage);
    ABUS_STORAGE_EXPORT(model_group, table, 1, group_storage);
    aBusHandle_t *handle = NULL;
    aBusSetKeyRequest_t set_request;
    aBusGetKeyRequest_t get_request;
#if ABUS_STATIC_ENABLE
    aBusHandle_t instance;
    aBusSigState_t states[2];

    aBusInstanceStructInit(&instance, states, 2);
    handle = &instance;
#define CREATE() aBusInitStatic(&table, &instance)
#define DESTROY() aBusDeInitStatic(handle)
#else
#define CREATE() aBusCreate(&table, &handle)
#define DESTROY() aBusDestroy(handle)
#endif

    aBusSetKeyRequestStructInit(&set_request);
    aBusGetKeyRequestStructInit(&get_request);
    assert(set_request.src == NULL && set_request.size == 0);
    assert(get_request.dst == NULL && get_request.size == 0);
    assert(aTimeoutIsValid(set_request.timeout));
    assert(aTimeoutIsValid(get_request.timeout));
    memcpy(defaults + 1, &unsigned_value, sizeof(unsigned_value));
    memcpy(defaults + 3, &signed_value, sizeof(signed_value));
    assert(CREATE() == A_STATUS_OK);
    assert(get_sig(handle, 1, output, 1, A_TIMEOUT_NO_WAIT) == 0);
    assert(output[0] == 0);
    input[0] = 255;
    assert(set_sig(handle, 1, input, 1, A_TIMEOUT_NO_WAIT) == 0);
    assert(get_sig(handle, 1, output, 1, A_TIMEOUT_NO_WAIT) == 0);
    assert(output[0] == 255);
    assert(get_sig(handle, 2, output, 16, A_TIMEOUT_NO_WAIT) == 0);
    assert(memcmp(output, defaults, 16) == 0);
    memcpy(input, defaults, 16);
    input[0] = 101;
    assert(set_sig(handle, 2, input, 16, A_TIMEOUT_NO_WAIT) ==
           A_STATUS_INVALID_PARAM);
    assert(get_sig(handle, 2, output, 16, A_TIMEOUT_NO_WAIT) == 0);
    assert(memcmp(output, defaults, 16) == 0);
    input[0] = 100;
    input[15] = 99;
    assert(set_sig(handle, 2, input, 16, A_TIMEOUT_NO_WAIT) == 0);
    assert(get_sig(handle, 2, output, 16, A_TIMEOUT_NO_WAIT) == 0);
    assert(memcmp(input, output, 16) == 0);
    /* 下标与业务键独立，两个入口操作相同快照。 */
    {
        aBusSetIndexRequest_t write;
        aBusGetIndexRequest_t read;

        aBusSetIndexRequestStructInit(&write);
        aBusGetIndexRequestStructInit(&read);
        write.sigIndex = 1;
        write.src = input;
        write.size = sizeof(input);
        read.sigIndex = 1;
        read.dst = output;
        read.size = sizeof(output);
        input[0] = 99;
        assert(aBusSetByIndex(handle, &write) == A_STATUS_OK);
        assert(get_sig(handle, 2, output, 16, A_TIMEOUT_NO_WAIT) == 0);
        assert(memcmp(input, output, 16) == 0);
        input[0] = 98;
        assert(set_sig(handle, 2, input, 16, A_TIMEOUT_NO_WAIT) == 0);
        assert(aBusGetByIndex(handle, &read) == A_STATUS_OK);
        assert(memcmp(input, output, 16) == 0);
        input[0] = 101;
        assert(aBusSetByIndex(handle, &write) == A_STATUS_INVALID_PARAM);
        assert(output[0] == 98);
        write.sigIndex = table.sig_count;
        read.sigIndex = SIZE_MAX;
        assert(aBusSetByIndex(handle, &write) == A_STATUS_NOT_FOUND);
        assert(aBusGetByIndex(handle, &read) == A_STATUS_NOT_FOUND);
        EXPECT_ASSERT(aBusSetByIndex(handle, NULL));
        EXPECT_ASSERT(aBusGetByIndex(handle, NULL));
    }
    EXPECT_ASSERT(aBusSetByKey(handle, NULL));
    EXPECT_ASSERT(aBusGetByKey(handle, NULL));
    assert(DESTROY() == A_STATUS_OK);
#if ABUS_DEF_CHECK_ENABLE
    params[0].type = ALIB_DATA_RAW;
    assert(CREATE() == A_STATUS_INVALID_PARAM);
    params[0].type = ALIB_DATA_U8;
    params[0].offset = SIZE_MAX;
    assert(CREATE() == A_STATUS_INVALID_PARAM);
    params[0].offset = 0;
    params[0].max.u8 = 41;
    assert(CREATE() == A_STATUS_INVALID_PARAM);
    params[0].max.u8 = 100;
    params[0].min.u8 = 101;
    assert(CREATE() == A_STATUS_INVALID_PARAM);
    params[0].min.u8 = 1;
    sigs[1].default_data = NULL;
    assert(CREATE() == A_STATUS_INVALID_PARAM);
    params[0].min.u8 = 0;
    sigs[1].params = NULL;
    assert(CREATE() == A_STATUS_INVALID_PARAM);
    sigs[1].params = params;
    sigs[0].size = 0;
    assert(CREATE() == A_STATUS_INVALID_PARAM);
    sigs[0].size = 1;
#endif
    sigs[1].default_data = NULL;
    sigs[1].params = NULL;
    sigs[1].param_count = 0;
    assert(CREATE() == A_STATUS_OK);
    memset(input, 0, sizeof(input));
    assert(get_sig(handle, 2, output, 16, A_TIMEOUT_NO_WAIT) == 0);
    assert(memcmp(input, output, 16) == 0);
    assert(DESTROY() == A_STATUS_OK);
    assert(live_allocations == 0 && live_locks == 0);
#undef CREATE
#undef DESTROY
}

static void binding_tests(void)
{
    static const aBusSig_t definition[] = {
        {.sigKey = 9, .size = sizeof(uint32_t)},
        {.sigKey = 3, .size = sizeof(uint32_t)}
    };
    static const aBusTable_t duplicate = {.sigs = definition, .sig_count = 2};
    static const aBusTable_t short_table = {
        .sigs = definition, .sig_count = 2
    };
    static const aBusTable_t bad_index = {
        .sigs = definition, .sig_count = 2
    };
    static uint32_t one, two;
    static uint8_t small;
    ABUS_STORAGE_EXPORT(dup1, duplicate, 0, one);
    ABUS_STORAGE_EXPORT(dup2, duplicate, 0, two);
    ABUS_STORAGE_EXPORT(short1, short_table, 0, small);
    ABUS_STORAGE_EXPORT(bad1, bad_index, 2, one);
    aBusHandle_t *handle = NULL;
#if ABUS_STATIC_ENABLE
    aBusHandle_t instance;
    aBusSigState_t states[2];

    aBusInstanceStructInit(&instance, states, 2);
    handle = &instance;
    assert(aBusInitStatic(&duplicate, handle) == A_STATUS_INVALID_PARAM);
    assert(aBusInitStatic(&short_table, handle) == A_STATUS_INVALID_PARAM);
    assert(aBusInitStatic(&bad_index, handle) == A_STATUS_INVALID_PARAM);
#endif
#if ABUS_DYNAMIC_ENABLE
    assert(aBusCreate(&duplicate, &handle) == A_STATUS_INVALID_PARAM);
    assert(handle == NULL);
    assert(aBusCreate(&short_table, &handle) == A_STATUS_INVALID_PARAM);
    assert(aBusCreate(&bad_index, &handle) == A_STATUS_INVALID_PARAM);
    {
        const aBusSig_t huge_sigs[] = {
            {.sigKey = 1, .size = SIZE_MAX},
            {.sigKey = 2, .size = 1}
        };
        const aBusTable_t huge = {.sigs = huge_sigs, .sig_count = 2};

        assert(aBusCreate(&huge, &handle) == A_STATUS_INVALID_PARAM);
        assert(handle == NULL && live_allocations == 0);
    }
    bindings_fill();
    alloc_budget = 1;
    assert(aBusCreate(&mixed_table, &handle) == A_STATUS_NO_MEMORY);
    assert(bindings_unchanged());
    alloc_budget = -1;
    assert(aBusCreate(&mixed_table, &handle) == A_STATUS_OK);
    assert(live_allocations == 2);
    assert(handle->sigs[0].data == bindings_first());
    assert(handle->sigs[1].data != NULL);
    io_tests(handle);
    assert(aBusDestroy(handle) == A_STATUS_OK);
    assert(aBusCreate(&bound_table, &handle) == A_STATUS_OK);
    assert(live_allocations == 1); /* 全绑定：不申请数据块。 */
    assert(aBusDestroy(handle) == A_STATUS_OK);
#endif
    assert(live_allocations == 0 && live_locks == 0);
}

int main(void)
{
    binding_tests();
    model_tests();
#if ABUS_STATIC_ENABLE
    static_tests();
#endif
#if ABUS_DYNAMIC_ENABLE
    dynamic_tests();
#endif
    assert(live_allocations == 0 && live_locks == 0);
    return 0;
}
