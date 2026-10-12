/**
 * @file aAtomic.h
 * @brief 原子类型、读写、读改写及内存屏障；按编译器选择 C11 后端。
 *
 * aAtomicBool_t / aAtomicInt_t / aAtomicUint_t 对应 bool / int / unsigned int。
 * A_ATOMIC_TYPE(T) 可声明其他原子类型；不保证大小、对齐或跨编译器 ABI。
 * 对象不得 packed，不得在使用期间复制、移动、memset 或重新初始化。
 * 所有并发访问均使用本接口，不能通过强制转换进行普通或 volatile 访问。
 *
 * 初始化：A_ATOMIC_VAR_INIT(value) / A_ATOMIC_FLAG_INIT 用于声明时初始化；
 * aAtomicInit(p, value) 用于对象尚未共享时初始化，不提供并发初始化保护。
 *
 * Load / Store / Exchange / FetchAdd / FetchSub / FetchAnd / FetchOr / FetchXor
 * 均提供 aAtomicXxx(p, ...) 和 aAtomicXxxExplicit(p, ..., order) 两种形式。
 * 非 Explicit 形式使用 A_ATOMIC_SEQ_CST；Exchange 和 Fetch 返回修改前的值。
 * Fetch 算术/位操作沿用 C11 的类型约束；无符号计数按类型宽度回绕。
 *
 * CompareExchangeStrong / Weak(p, expected, desired) 比较并交换，返回是否成功；
 * 失败时把读到的实际值写回 *expected，Weak 允许伪失败，调用者须按需循环。
 * Explicit 形式另接收 success_order、failure_order；failure 不能是 RELEASE/
 * ACQ_REL，也不能强于 success。expected 是调用者独占的普通变量指针。
 *
 * Load 只接受 RELAXED/CONSUME/ACQUIRE/SEQ_CST；Store 只接受
 * RELAXED/RELEASE/SEQ_CST。读改写可接受全部内存序。内存序应为编译期常量。
 * RELAXED 只保证自身原子性，不发布其他数据；跨对象发布使用 release/acquire。
 * ThreadFence 提供线程间内存屏障；SignalFence 仅限制编译器重排。
 * 屏障不代替 DMA cache 维护，也不能单独使普通变量的并发访问合法。
 *
 * aAtomicIsLockFree(p) 查询具体对象；A_ATOMIC_*_LOCK_FREE 的 0/1/2 分别表示
 * 从不/有时/始终无锁。ISR 路径须确认其类型始终无锁，不允许隐藏的库锁。
 * aAtomicFlag_t 始终无锁，但使用 FlagTestAndSet 构造自旋锁仍可能在 ISR 死锁。
 * 不根据单核/多核削弱语义，不分配对象、不调用 aOS/aDrv，也不检查 CPU 核数。
 */
#ifndef A_ATOMIC_H
#define A_ATOMIC_H

#if defined(__cplusplus)
#error "aAtomic currently supports C11, not C++"
#elif !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#error "aAtomic requires C11 or later"
#elif defined(__clang__)
/* Clang 也定义 __GNUC__，必须优先识别；clangd 使用此后端解析。 */
#include "atomic/aAtomic_clang.h"
#elif defined(__GNUC__)
#include "atomic/aAtomic_gcc.h"
#else
#error "aAtomic: add a backend for this compiler (GCC/Clang C11 required)"
#endif

#endif
