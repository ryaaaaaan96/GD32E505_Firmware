/* Clang 后端：仅映射工具链 C11 原子，不增加调用、对象或运行时分支。 */
#ifndef A_ATOMIC_CLANG_H
#define A_ATOMIC_CLANG_H

#ifndef A_ATOMIC_H
#error "Include aAtomic.h instead of the backend header"
#endif
#if !defined(__clang__)
#error "aAtomic_clang.h requires Clang"
#endif
#if defined(__STDC_NO_ATOMICS__) && __STDC_NO_ATOMICS__
#error "The selected target does not support C11 atomics"
#endif

#include <stdatomic.h>

typedef atomic_bool aAtomicBool_t;
typedef atomic_int aAtomicInt_t;
typedef atomic_uint aAtomicUint_t;
typedef atomic_flag aAtomicFlag_t;
typedef memory_order aAtomicMemoryOrder_t;

#define A_ATOMIC_TYPE(type_) _Atomic(type_)
#define A_ATOMIC_VAR_INIT(value_) (value_)
#define A_ATOMIC_FLAG_INIT ATOMIC_FLAG_INIT

#define A_ATOMIC_RELAXED memory_order_relaxed
#define A_ATOMIC_CONSUME memory_order_consume
#define A_ATOMIC_ACQUIRE memory_order_acquire
#define A_ATOMIC_RELEASE memory_order_release
#define A_ATOMIC_ACQ_REL memory_order_acq_rel
#define A_ATOMIC_SEQ_CST memory_order_seq_cst

#define A_ATOMIC_BOOL_LOCK_FREE ATOMIC_BOOL_LOCK_FREE
#define A_ATOMIC_CHAR_LOCK_FREE ATOMIC_CHAR_LOCK_FREE
#define A_ATOMIC_SHORT_LOCK_FREE ATOMIC_SHORT_LOCK_FREE
#define A_ATOMIC_INT_LOCK_FREE ATOMIC_INT_LOCK_FREE
#define A_ATOMIC_LONG_LOCK_FREE ATOMIC_LONG_LOCK_FREE
#define A_ATOMIC_LLONG_LOCK_FREE ATOMIC_LLONG_LOCK_FREE
#define A_ATOMIC_POINTER_LOCK_FREE ATOMIC_POINTER_LOCK_FREE

#define aAtomicInit(p_, value_) atomic_init((p_), (value_))
#define aAtomicIsLockFree(p_) atomic_is_lock_free((p_))
#define aAtomicLoad(p_) atomic_load((p_))
#define aAtomicLoadExplicit(p_, order_) atomic_load_explicit((p_), (order_))
#define aAtomicStore(p_, value_) atomic_store((p_), (value_))
#define aAtomicStoreExplicit(p_, value_, order_) \
    atomic_store_explicit((p_), (value_), (order_))
#define aAtomicExchange(p_, value_) atomic_exchange((p_), (value_))
#define aAtomicExchangeExplicit(p_, value_, order_) \
    atomic_exchange_explicit((p_), (value_), (order_))

#define aAtomicCompareExchangeStrong(p_, expected_, desired_) \
    atomic_compare_exchange_strong((p_), (expected_), (desired_))
#define aAtomicCompareExchangeStrongExplicit(p_, expected_, desired_, \
                                             success_, failure_) \
    atomic_compare_exchange_strong_explicit((p_), (expected_), (desired_), \
                                             (success_), (failure_))
#define aAtomicCompareExchangeWeak(p_, expected_, desired_) \
    atomic_compare_exchange_weak((p_), (expected_), (desired_))
#define aAtomicCompareExchangeWeakExplicit(p_, expected_, desired_, \
                                           success_, failure_) \
    atomic_compare_exchange_weak_explicit((p_), (expected_), (desired_), \
                                           (success_), (failure_))

#define aAtomicFetchAdd(p_, value_) atomic_fetch_add((p_), (value_))
#define aAtomicFetchAddExplicit(p_, value_, order_) \
    atomic_fetch_add_explicit((p_), (value_), (order_))
#define aAtomicFetchSub(p_, value_) atomic_fetch_sub((p_), (value_))
#define aAtomicFetchSubExplicit(p_, value_, order_) \
    atomic_fetch_sub_explicit((p_), (value_), (order_))
#define aAtomicFetchAnd(p_, value_) atomic_fetch_and((p_), (value_))
#define aAtomicFetchAndExplicit(p_, value_, order_) \
    atomic_fetch_and_explicit((p_), (value_), (order_))
#define aAtomicFetchOr(p_, value_) atomic_fetch_or((p_), (value_))
#define aAtomicFetchOrExplicit(p_, value_, order_) \
    atomic_fetch_or_explicit((p_), (value_), (order_))
#define aAtomicFetchXor(p_, value_) atomic_fetch_xor((p_), (value_))
#define aAtomicFetchXorExplicit(p_, value_, order_) \
    atomic_fetch_xor_explicit((p_), (value_), (order_))

#define aAtomicFlagTestAndSet(p_) atomic_flag_test_and_set((p_))
#define aAtomicFlagTestAndSetExplicit(p_, order_) \
    atomic_flag_test_and_set_explicit((p_), (order_))
#define aAtomicFlagClear(p_) atomic_flag_clear((p_))
#define aAtomicFlagClearExplicit(p_, order_) atomic_flag_clear_explicit((p_), (order_))
#define aAtomicThreadFence(order_) atomic_thread_fence((order_))
#define aAtomicSignalFence(order_) atomic_signal_fence((order_))
#define aAtomicKillDependency(value_) kill_dependency((value_))

#endif
