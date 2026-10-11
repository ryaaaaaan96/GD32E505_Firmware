#include "aAtomic.h"
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>

static aAtomicBool_t ready = A_ATOMIC_VAR_INIT(0);
static aAtomicUint_t count = A_ATOMIC_VAR_INIT(0U);
static aAtomicUint_t cas_count = A_ATOMIC_VAR_INIT(0U);
static aAtomicUint_t winners = A_ATOMIC_VAR_INIT(0U);
static aAtomicBool_t claimed = A_ATOMIC_VAR_INIT(0);
static aAtomicFlag_t flag = A_ATOMIC_FLAG_INIT;
static unsigned payload[2];
enum { THREADS = 4, ITERATIONS = 20000 };

static void *writer(void *argument)
{
    (void)argument;
    while (!aAtomicLoadExplicit(&ready, A_ATOMIC_ACQUIRE)) sched_yield();
    /* 发布者的普通数据由 ready 的 release/acquire 建立可见性。 */
    assert(payload[0] == 123U && payload[1] == 456U);
    if (!aAtomicExchangeExplicit(&claimed, 1, A_ATOMIC_RELAXED))
        (void)aAtomicFetchAdd(&winners, 1U);
    for (unsigned i = 0U; i < ITERATIONS; ++i) {
        (void)aAtomicFetchAddExplicit(&count, 1U, A_ATOMIC_RELAXED);
        unsigned expected = aAtomicLoadExplicit(&cas_count, A_ATOMIC_RELAXED);
        while (!aAtomicCompareExchangeWeakExplicit(&cas_count, &expected,
                   expected + 1U, A_ATOMIC_RELAXED, A_ATOMIC_RELAXED)) {}
    }
    return NULL;
}

static void value_tests(void)
{
    aAtomicInt_t number;
    aAtomicBool_t boolean;
    aAtomicUint_t bits;
    A_ATOMIC_TYPE(int *) pointer;
    int value = 23;
    int *expected_pointer = NULL;
    aAtomicInit(&number, -7);
    aAtomicInit(&boolean, 0);
    aAtomicInit(&bits, UINT_MAX);
    aAtomicInit(&pointer, NULL);
    assert(aAtomicIsLockFree(&number));
    assert(aAtomicIsLockFree(&boolean));
    assert(aAtomicIsLockFree(&bits));
    assert(aAtomicLoad(&number) == -7);
    assert(!aAtomicExchange(&boolean, 1));
    assert(aAtomicLoad(&boolean));
    aAtomicStore(&boolean, 0);
    assert(!aAtomicLoad(&boolean));
    assert(aAtomicExchangeExplicit(&number, -3, A_ATOMIC_ACQ_REL) == -7);
    aAtomicStoreExplicit(&number, -9, A_ATOMIC_RELEASE);
    assert(aAtomicLoadExplicit(&number, A_ATOMIC_ACQUIRE) == -9);
    int expected = 10;
    assert(!aAtomicCompareExchangeStrong(&number, &expected, 12));
    assert(expected == -9 && aAtomicLoad(&number) == -9);
    assert(aAtomicCompareExchangeStrongExplicit(&number, &expected, 12,
                                                A_ATOMIC_ACQ_REL, A_ATOMIC_ACQUIRE));
    assert(expected == -9 && aAtomicLoad(&number) == 12);
    expected = 12;
    while (!aAtomicCompareExchangeWeak(&number, &expected, -10)) {}
    assert(aAtomicLoad(&number) == -10);

    assert(aAtomicCompareExchangeStrong(&pointer, &expected_pointer, &value));
    assert(aAtomicLoadExplicit(&pointer, A_ATOMIC_CONSUME) == &value);
    assert(aAtomicKillDependency(aAtomicLoad(&pointer)) == &value);

    assert(aAtomicFetchAdd(&bits, 1U) == UINT_MAX);
    assert(aAtomicLoad(&bits) == 0U);
    assert(aAtomicFetchSubExplicit(&bits, 1U, A_ATOMIC_RELAXED) == 0U);
    assert(aAtomicLoad(&bits) == UINT_MAX);
    aAtomicStore(&bits, 3U);
    assert(aAtomicFetchAnd(&bits, 2U) == 3U);
    assert(aAtomicFetchOrExplicit(&bits, 4U, A_ATOMIC_RELAXED) == 2U);
    assert(aAtomicFetchXor(&bits, 5U) == 6U);
    assert(aAtomicFetchAndExplicit(&bits, 1U, A_ATOMIC_RELAXED) == 3U);
    assert(aAtomicFetchOr(&bits, 2U) == 1U);
    assert(aAtomicFetchXorExplicit(&bits, 1U, A_ATOMIC_RELAXED) == 3U);
    assert(aAtomicFetchSub(&bits, 1U) == 2U);
    assert(aAtomicLoad(&bits) == 1U);

    assert(!aAtomicFlagTestAndSet(&flag));
    assert(aAtomicFlagTestAndSetExplicit(&flag, A_ATOMIC_ACQUIRE));
    aAtomicFlagClearExplicit(&flag, A_ATOMIC_RELEASE);
    assert(!aAtomicFlagTestAndSet(&flag));
    aAtomicFlagClear(&flag);
    aAtomicThreadFence(A_ATOMIC_SEQ_CST);
    aAtomicSignalFence(A_ATOMIC_SEQ_CST);
}

static void argument_tests(void)
{
    aAtomicUint_t values[5];
    unsigned i = 0U, input = 7U;
    aAtomicInit(&values[i++], input++);
    aAtomicInit(&values[i++], input++);
    assert(i == 2U && input == 9U);
    i = 0U;
    assert(aAtomicLoadExplicit(&values[i++], A_ATOMIC_RELAXED) == 7U);
    aAtomicStoreExplicit(&values[i++], input++, A_ATOMIC_RELAXED);
    assert(i == 2U && input == 10U && aAtomicLoad(&values[1]) == 9U);
    i = 0U;
    assert(aAtomicFetchAddExplicit(&values[i++], input++, A_ATOMIC_RELAXED) == 7U);
    assert(i == 1U && input == 11U && aAtomicLoad(&values[0]) == 17U);
    unsigned expected[] = {0U, 9U};
    unsigned j = 0U;
    assert(!aAtomicCompareExchangeStrongExplicit(&values[i++], &expected[j++],
                   input++, A_ATOMIC_ACQ_REL, A_ATOMIC_RELAXED));
    assert(i == 2U && j == 1U && input == 12U && expected[0] == 9U);
}

int main(void)
{
    value_tests();
    argument_tests();
    pthread_t threads[THREADS];
    for (unsigned i = 0U; i < THREADS; ++i)
        assert(pthread_create(&threads[i], NULL, writer, NULL) == 0);
    payload[0] = 123U;
    payload[1] = 456U;
    aAtomicStoreExplicit(&ready, 1, A_ATOMIC_RELEASE);
    for (unsigned i = 0U; i < THREADS; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(aAtomicLoad(&count) == THREADS * ITERATIONS);
    assert(aAtomicLoad(&cas_count) == THREADS * ITERATIONS);
    assert(aAtomicLoad(&winners) == 1U);
    puts("aAtomic values, single evaluation, CAS, publication and concurrency passed");
    return 0;
}
