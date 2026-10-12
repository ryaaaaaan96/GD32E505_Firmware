# aLib

不依赖 MCU、RTOS、链接脚本或 C 运行库的纯 C 基础层，提供统一布尔、状态、
超时与错误类型、`aStatusToErrno()` 状态映射、编译器属性、原子接口和纯计算工具。errno
的任务局部存储仍由 aOS 提供；GCC newlib syscall 不属于 aLib。

## 原子接口

自有代码统一包含 `aAtomic.h`，不直接使用 `<stdatomic.h>`。当前仅实现
GCC C11 后端，映射集中在 `include/atomic/aAtomic_gcc.h`；Keil、IAR、Clang
后端尚未实现，不会在未知编译器上静默退化为普通变量或空操作。
接口为宏映射，保留类型和内存序，不增加对象、函数调用或 CPU 核数判断。

| 内容 | 接口 |
| --- | --- |
| 常用类型 | `aAtomicBool_t`、`aAtomicInt_t`、`aAtomicUint_t`、`aAtomicFlag_t` |
| 其他原子类型 | `A_ATOMIC_TYPE(T)`，例如 `A_ATOMIC_TYPE(uint32_t)` |
| 初始化 | `A_ATOMIC_VAR_INIT(value)`、`A_ATOMIC_FLAG_INIT`、`aAtomicInit(p, value)` |
| 读写、交换 | `aAtomicLoad`、`aAtomicStore`、`aAtomicExchange` |
| 比较交换 | `aAtomicCompareExchangeStrong`、`aAtomicCompareExchangeWeak` |
| 计数、位操作 | `aAtomicFetchAdd/Sub/And/Or/Xor`，返回旧值 |
| 原子标志 | `aAtomicFlagTestAndSet`、`aAtomicFlagClear` |
| 屏障 | `aAtomicThreadFence`、`aAtomicSignalFence`、`aAtomicKillDependency` |
| 无锁能力 | `aAtomicIsLockFree(p)`、`A_ATOMIC_*_LOCK_FREE` |

读写、交换、比较交换、Fetch 和 Flag 操作均有 `Explicit` 后缀版本，末尾
指定内存序；默认使用 `A_ATOMIC_SEQ_CST`。比较交换的 Explicit 版本分别指定
成功和失败的内存序，失败时更新 `*expected`，Weak 允许伪失败。
内存序为 `A_ATOMIC_RELAXED/CONSUME/ACQUIRE/RELEASE/ACQ_REL/SEQ_CST`，类型为
`aAtomicMemoryOrder_t`；合法组合见头文件契约。

```c
#include "aAtomic.h"

static aAtomicUint_t dropped = A_ATOMIC_VAR_INIT(0U);

/* 独立计数，不承担其他数据的发布。 */
(void)aAtomicFetchAddExplicit(&dropped, 1U, A_ATOMIC_RELAXED);
unsigned snapshot = aAtomicLoadExplicit(&dropped, A_ATOMIC_RELAXED);
```

初始化必须在共享对象前完成，初始化不提供并发保护。RELAXED 只保证单次
原子访问；发布其他数据需要匹配的 release/acquire 或其他同步机制。
对象保持自然对齐，不得 packed、在活动期间复制或通过普通指针访问。
单核仍存在任务/ISR 抢占，不按核数去掉读改写保护；多个关联字段仍需 aOS
临界区等机制维持整体一致，DMA cache 维护也不由本接口完成。

ISR 使用的类型必须始终无锁（对应能力宏为 2）。GCC 对其他类型可能生成
`libatomic` 调用，应用需提供链接支持，但不能把这种库锁路径用于 ISR。
当前日志和 Shell 保留编译期无锁检查；不引入基于 aOS 的后备锁。

验证：`SANITIZE=1 python3 tests/atomic/run.py`。覆盖宏单次求值、初始化、
旧值返回、比较交换失败回写、计数回绕、多线程计数与发布可见性；对比主机
GCC `-O2` 下封装与原生 C11 的生成指令。测试不代表 Keil/IAR 或 ARM 板上验证。

## 整数计算

`ALIB_DIV_ROUND_UP(value, divisor)` 对无符号整数除法向上取整，
例如 `ALIB_DIV_ROUND_UP(1025U, 4U)` 返回 `257` 个单元，
不会返回补齐后的 `1028` 字节。商和余数的写法避免相加后再除造成溢出。
除数必须大于零；宏参数会重复求值，须无副作用且求值期间保持稳定。
FreeRTOS 栈元素类型和默认栈容量仍由 aOS 后端管理。

## CRC32

`aCrc.h` 提供 `aCrc32(crc, data, size)`，采用 CRC-32/ISO-HDLC（IEEE），
与常见 CRC32/zlib 结果一致。首次传入 0，分段时传入上次返回值；空段返回
传入值，只有长度为零时允许空指针。支持未对齐输入，无状态、无内存分配。
沿用 aLib 的头文件实现方式，4 位查表占用 64 字节常量表。

```c
#include "aCrc.h"

uint32_t crc = aCrc32(0U, header, header_size);
crc = aCrc32(crc, payload, payload_size);
```

接口只处理字节，字段排列与字节序由调用方决定。aDataBase 在初始化时使用它
计算 SIG 布局标记；FlashDB 内部的记录 CRC 继续使用上游实现。
验证：`python3 tests/crc/run.py`，覆盖 C11/C++11、标准向量、zlib 对照、
非零初始 CRC、空段、分段和未对齐输入。

## 可选布局断言

`aLayout.h` 提供编译期检查，也由 `aLib.h` 包含。由定义结构体的应用层
选择调用，C11/C++11 均支持；不受 `NDEBUG` 影响，不产生运行时代码。

| 宏 | 检查内容 |
| --- | --- |
| `ALIB_STATIC_ASSERT(condition, message)` | 常量条件成立，否则报告指定消息 |
| `ALIB_ASSERT_TYPE_SIZE(type, bytes)` | 类型大小等于指定常量字节数 |
| `ALIB_ASSERT_MEMBER_OFFSET(type, member, offset)` | 成员位于指定常量偏移 |

```c
#include "aLayout.h"
#include <stdint.h>

typedef struct {
    uint16_t mode;
    uint16_t reserved;
    uint32_t speed;
} Example_t;

/* 应用决定是否检查结构体总大小等于成员大小之和。 */
ALIB_ASSERT_TYPE_SIZE(Example_t,
    sizeof(((Example_t *)0)->mode) +
    sizeof(((Example_t *)0)->reserved) +
    sizeof(((Example_t *)0)->speed));

/* 如需固定对外布局，可额外声明预期总大小和成员偏移。 */
ALIB_ASSERT_TYPE_SIZE(Example_t, 8U);
ALIB_ASSERT_MEMBER_OFFSET(Example_t, speed, 4U);
```

调用放在完整类型定义之后，并以分号结束。成员清单由调用者保证完整且不重复；
总长度相等不能独立证明成员顺序、连续性或没有重叠。`sizeof` 中的成员表达式
不会访问空指针。检查适用于固定大小类型，偏移检查不支持位域，C++ 使用
标准布局类型。宏不修改对齐方式，也不保证字节序或并发访问安全。

普通 C 的 `const` 描述数组元素不能用于编译期断言。如果长度来自 aBus 的
`params[]`，应用可调用 `aBusSigParamSizeCheck()`，见
[aBus 可选长度检查](../../func/aBus/README.md#可选参数长度检查)。
aLib 不依赖 aBus，也不强制两字节寄存器规则。

验证：`python3 tests/layout/run.py` 检查 C11/C++11、Debug/Release 下的通过
和预期编译失败，以及 aBus 长度检查的溢出边界。需要主机 C/C++ 编译器。

## 布尔类型

项目自研代码统一使用 `aBool_t`、`A_TRUE` 和 `A_FALSE`，不在其他层直接暴露
标准 C `bool`。`aBool_t` 封装标准 C 布尔类型，只表达软件逻辑，不承诺固定存储
尺寸、结构体布局或外部 ABI。

以下场景不得使用 `aBool_t`：

- 外设寄存器、DMA 数据和位掩码；
- 通信协议、文件、Flash 或数据库持久化格式；
- 必须固定宽度或跨编译器共享的二进制接口。

这些边界使用 `uint8_t`、`uint16_t`、`uint32_t` 等固定宽度类型，并通过显式
编码/解码转换为 `aBool_t`。多个可组合状态使用无符号整数位掩码，不使用 C
位域；位掩码作为条件时必须与 `0U` 显式比较。

FlashDB、FreeRTOS、CMSIS 和 GD32 标准外设库等第三方源码保留各自原始布尔
类型。项目封装层负责在第三方 API 边界转换，避免修改上游源码。

## aFifo.h

调用者提供固定容量、固定元素大小的存储；Init/Push/Peek/Pop/Count。
不分配内存，不加锁，调用方必须串行化同一 FIFO 的全部操作。
队满返回 BUSY，队空返回 NOT_READY，失败不改变索引。

元素按值复制。保存指针的元素只复制指针，不复制或释放 payload。
USART 使用 FIFO 保存 TX 请求描述符；超时、取消、回调、DMA 调度均留在 device。
FIFO 不是无锁 SPSC 队列，也不直接向 DMA 出借槽位。

测试：`python3 tests/usart/run.py` 包含 FIFO 空/满、回绕、Peek 和非法容量检查。
