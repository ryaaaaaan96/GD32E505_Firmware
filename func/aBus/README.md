# aBus 板内 RAM 数据总线设计说明

## 1. 目标和层级

aBus 用于单片机内部软件模块在 RAM 中交换数据。一个 `sigID` 表示一组数据；
这组数据可以是单个数值，也可以是包含多个参数的结构体。
以 `sigID` 为整体读写单位：一次成功写入使整组数据同时生效，
一次读取获得一次完整写入对应的数据快照。

组内每个参数可以有各自的类型、范围和 `flags`。
这些规则是静态定义，使用 `const` 表放在 Flash；当前值放在 RAM。

## 2. 核心结构体

```c
#include <stddef.h>
#include <stdint.h>

/* 通用数值标签与联合体位于 aLib/include/aScalar.h。 */
#include "aScalar.h"

/* Flash：总线字段规则仍归 aBus，联合体成员必须与 type 一致。 */
typedef struct {
    uint16_t offset; /* 相对整组数据起始位置的字节偏移。 */
    uint16_t flags;  /* 保留且必须为零，与 offset 相邻以减少填充。 */
    aScalarType_t type;
    aScalar_t min;
    aScalar_t max;
    aScalar_t default_value;
} aBus_ItemDef;

/* RAM：整组数据的存储引用。 */
typedef struct {
    void *data;
#if ABUS_LOCK_MODE == ABUS_LOCK_SIG
    aOSMutex_t mutex; /* 初始为 NULL，由 aBus 创建和释放。 */
#endif
} aBus_SigState;

/* Flash：一组数据的定义，显式关联其 RAM 状态。 */
#define ABUS_SIG_FLAG_LOCK (1U << 0)

typedef struct {
    uint16_t sigID;
    uint16_t size;
    /* ABUS_SIG_FLAG_LOCK：整组 Get/Set 使用内部锁。
     * 未设置时，由应用保证没有读写竞争或提供外部同步。 */
    uint16_t flags;
    uint16_t itemCount;
    const aBus_ItemDef *items;
    aBus_SigState *state;
} aBus_SigDef;
```

`aBus_SigDef.state` 明确指向对应的 `aBus_SigState`，不依赖两张表的排列顺序。
`state->data` 指向这组数据的 RAM 起始地址。
item 不必拥有独立 RAM 地址；其位置由 `offset` 给出，也不必保存 `itemID`。
`aBus_SigDef.flags` 是组级属性，与 `aBus_ItemDef.flags` 的字段级规则独立。
锁标志只定义在组级，不从校验项是否存在推断该组是否需要同步。

## 3. 定义示例

```c
enum { SIG_MOTOR = 1 };

typedef struct {
    int32_t speed;
    uint16_t temperature;
    uint8_t enable;
} MotorData;

static MotorData motorData;               /* RAM */
static aBus_SigState motorState = {        /* RAM */
    .data = &motorData
};

static const aBus_ItemDef motorItems[] = { /* Flash */
    {
        .offset = offsetof(MotorData, speed),
        .type = ALIB_SCALAR_I32,
        .min.i32 = -5000,
        .max.i32 = 5000,
        .default_value.i32 = 0,
    },
    {
        .offset = offsetof(MotorData, temperature),
        .type = ALIB_SCALAR_U16,
        .min.u16 = 0,
        .max.u16 = 150,
        .default_value.u16 = 25,
    },
    {
        .offset = offsetof(MotorData, enable),
        .type = ALIB_SCALAR_U8,
        .min.u8 = 0,
        .max.u8 = 1,
        .default_value.u8 = 0,
    },
};

static const aBus_SigDef motorSig = {      /* Flash */
    .sigID = SIG_MOTOR,
    .size = sizeof(MotorData),
    .flags = ABUS_SIG_FLAG_LOCK,
    .items = motorItems,
    .itemCount = sizeof(motorItems) / sizeof(motorItems[0]),
    .state = &motorState,
};
```

- 若一个 `sigID` 只挂一个数值，数据区直接定义为该数值类型，
  唯一参数的 `offset` 为 `0`。
- 若某个字段无需范围检查，可以不列入 `items`；结构体本身仍按整组复制，
  该字段的初始值也由应用负责。需要总线默认值但无需额外限幅的字段，
  可以登记并将范围设为该类型的完整范围。
- 本版 `aScalarType_t` 仅支持上述四种整数类型，暂不扩展。
  结构体可以包含其他类型的成员，未列入 `items` 的成员只参与整组复制，
  不做范围校验。以后需要校验其他类型时，再增加对应的读取与校验分支。

## 4. 读写和原子性

接口以整组数据为单位：

```c
aStatus_t aBusSetSig(uint16_t sigID, const void *src, uint16_t size,
                     aTimeout_t timeout);
aStatus_t aBusGetSig(uint16_t sigID, void *dst, uint16_t size,
                     aTimeout_t timeout);
```

### 4.1 完整替换与发布入口

`SetSig` 是完整替换，不合并字段。每组数据指定一个组装发布入口；
其他模块可以向该入口提供数据，不应各自读取旧组、修改字段后整组写回。
若入口可能被多个任务同时调用，应用仍须串行化组装过程。
总线的复制锁不解决发生在调用前的“读取—修改—写回”覆盖问题。

`SetSig` 找到定义并通过断言核对 `size`，再按每条 `aBus_ItemDef` 的类型和偏移，
从 `src` 读取参数、检查范围。全部通过后，才在同步保护下把整组复制到
`state->data`。校验失败时不改动旧数据。

读取在相同的同步规则下复制整组，不能返回内部可写指针。
应用提供数据区并初始化未登记成员；aBusInit 在检查和资源创建成功后，
把已登记字段设置为 default_value。首次发布前 Get 返回此初始内容。
重新 DeInit/Init 会再次应用字段默认值；重复 Init 返回 BUSY，不重置数据。
总线不记录采集有效性或更新版本，需要这些信息的业务可在自己的数据结构中表达。

调用期间，应用须保证 `src` 内容稳定，`dst` 不被其他上下文并发访问。
总线内部锁不保护调用者自己的缓冲区；不能校验一份数据却复制另一份已变化的数据。

### 4.2 初始化检查与查找

`ABUS_DEF_CHECK_ENABLE=ON` 时，初始化检查定义，包括：

- 组大小非零，状态及数据地址有效，不同组的数据区不重叠。
- `itemCount` 非零时 `items` 必须有效；无校验项时允许 NULL。
- 类型受支持，字段偏移和类型大小均落在组数据范围内，计算不溢出。
- 上下限及默认值使用 type 对应的联合体成员，满足 min <= default <= max。
- 同一组的登记字段不能重复或重叠，避免默认值相互覆盖。

`sigID` 唯一性不在设备初始化时检查，由应用保证。
后续计划在确定 XML 等数据定义源后，由 Python 使用集合检查重复 ID，
校验失败时阻止构建，并从同一数据源生成 C 定义表。
当前尚未接入 Python 校验，不能认为构建已保证唯一性，也不通过正则解析 C 表。
重复 ID 属于无效配置；现有查找会命中第一项，后续同 ID 组无法独立访问。
定义校验开启时仍检查组间数据区及同组字段重叠，包含对应的嵌套循环。

定义初始化后保持不变。每次 `SetSig` 不重复检查静态定义。
调用参数使用标准 `assert()`；组查找和输入值的范围校验始终执行。
读取输入字段时应避免未对齐的类型指针解引用。

组数较少时优先采用线性查找，不先引入句柄接口。
确实出现高频访问且组数较多时，再根据测量结果考虑直接索引或缓存句柄。

### 4.3 锁的粒度与免锁条件

这里的原子性指读者只能获得写入前或写入后的整组数据，不能读到部分新值。
顶层模式决定锁资源的创建，组级 `ABUS_SIG_FLAG_LOCK` 仅决定读写时是否使用锁。
`NONE` 模式下即使设置了组标志也不会加锁，应用须保证访问串行化。
标志在初始化前确定，运行中不得切换。启用时 Get 和 Set 都必须参与锁协议，
不能只保护写者而让读者直接复制共享数据。

锁模式在 `config/aclass_config.cmake` 中配置，与组标志无关：

| 模式 | 初始化创建的资源 | 读写时的行为 |
| --- | --- | --- |
| `NONE` 无锁 | 不编译锁成员、锁变量和锁操作 | 所有组都不使用内部锁 |
| `BUS` 整表锁 | 内部 bus_mutex 保存固定的一把锁 | 设置锁标志的组使用这一把锁 |
| `SIG` 条目锁 | 每组 state->mutex 保存自己的锁 | 设置锁标志的组使用对应锁 |

这里的设备锁指 aBus 实例的共享锁，不复用串口等外设驱动的锁。
本版不按 item 成员分别加锁；一次整组复制只获取一次对应的锁。
输入范围校验可在锁外进行，锁内只处理共享数据的复制及必要的内部状态。
Get 与 Set 必须使用同一保护规则。
`ABUS_LOCK_GRANULARITY` 支持 `NONE`、`BUS`、`SIG`，默认仍为 `BUS`。
锁的存储和创建/释放/读写路径通过 `#if` 选择，不依赖常量条件的编译器优化。
BUS 模式编译内部的 `bus_mutex`；SIG 模式在 `aBus_SigState` 中编译 `mutex` 成员。
NONE/BUS 模式的状态结构不含锁成员，NONE 模式不引用 aOS 锁或内存接口。
aBus 不再动态分配锁指针表，锁对象本身仍由 aOSMutexCreate 创建。
SIG 模式使用指定成员初始化，例如 `{.data = &motorData}`，使 mutex 初始为 NULL。
创建中途失败只回收本次已成功创建的锁，数据默认值尚未写入；销毁后锁槽清空。
即使所有组的锁标志都为零，BUS 仍创建一把锁，SIG 仍为全部组创建锁。

是否可以免锁，与锁粒度是两个不同问题：

- 单一执行上下文访问，或调用方已保证所有读写串行化的组，可以考虑免内部锁。
- 数据初始化完成后不再写入的组，可以在安全发布后并发读取。
- 普通结构体存在并发读写时，不能仅因业务“不在意原子性”就取消同步。
  即使仅有一个写者，其他任务并发读取普通内存仍存在数据竞争。
- 免锁是整组访问契约，不按某些成员是否重要决定。
  需要独立同步策略的数据可拆组；采用原子字段等其他机制需要另行设计。

本版先保留整组一致性，不引入允许读取半更新结构的模式。
未设置锁标志仅表示读写时不获取内部锁，不表示普通内存自动获得原子访问能力。
应用必须满足上述免内部锁条件；独立原子字段路径不由此标志隐式启用。

互斥锁接口仅能用于任务上下文，不能直接在 ISR 中获取。
先前提出的 ISR 访问需求仍需单独确定非阻塞路径，不能由锁粒度配置自动解决。
当前文档未承诺任意结构大小均可在 ISR 中直接读写。

### 4.4 读写锁候选方案

组级锁可以采用读写锁，但“是否启用锁”“锁的粒度”和“锁的种类”是独立选择。
`ABUS_SIG_FLAG_LOCK` 表达保护要求，不把公共定义绑定到具体锁实现。

- Get 获取读锁，复制完整数据后释放；同一把锁允许多个读者共同持有。
- Set 在锁外完成输入校验，获取写锁后复制完整数据，再释放写锁。
- 写锁与读锁、其他写锁互斥。读写两端不能使用互不关联的两把普通锁。
- 不在持锁期间发送事件、调用业务回调、执行 Flash 保存或其他慢速 I/O。
- 获取锁失败时不能继续复制；现有接口显式传入锁等待超时，返回 aOS 错误。

若实现读写锁，应阻止等待中的写者被不断到来的新读者永久越过，
并禁止递归获取及持读锁升级为写锁，避免隐含死锁。
锁属于运行时对象，不能放入 Flash 定义表；创建失败须回收此前创建的全部锁。

当前 aOS 只提供普通及递归互斥锁，尚无读写锁接口。
跨平台读写锁应由 aOS 提供，再由 aBus 使用，不直接绑定某个 OS 的类型。
读写锁不保证比普通互斥锁快：当前单核 MCU 上多个读者不会真正并行执行，
短小结构复制时额外的读者计数、等待管理和优先级处理可能得不偿失。
第一版已采用普通互斥锁，读写锁保留为待评估方案，当前未实现。

## 5. 事件与待定项

事件机制与当前值存储分开。写入成功后可按 item 的通知规则触发事件；
事件只表示“有更新”时，消费者再读取整组快照。

若要求每次变化都被处理，必须保留每次变化的数据，例如将快照放入消息队列。
序号只能用于发现漏处理，不能恢复已被覆盖的旧数据；
只有另有历史存储时，才能通过序号定位相应数据。


## 6. 第一版代码与使用

公共头文件为 `include/aBus.h`，实现为 `src/aBus.c`。
产品配置 `ABUS_ENABLE` 控制是否构建模块；使用模块的目标需链接 `aBus`。
当前不自动注册应用数据，也不自动在应用启动流程中初始化。

第一版为单例，生命周期由应用串行化。定义表是连续的 `aBus_SigDef` 数组，
sigID 无需连续或排序，按线性查找定位；不提供逐组缓存句柄。

```c
static const aBus_SigDef signals[] = {
    {
        .sigID = SIG_MOTOR,
        .size = sizeof(MotorData),
        .flags = ABUS_SIG_FLAG_LOCK,
        .items = motorItems,
        .itemCount = sizeof(motorItems) / sizeof(motorItems[0]),
        .state = &motorState,
    },
};

/* 在应用初始化函数中，局部变量放在函数头部。 */
aBusConfig_t config;
aStatus_t status;

aBusConfigStructInit(&config);
config.signals = signals;
config.signal_count = sizeof(signals) / sizeof(signals[0]);
status = aBusInit(&config);
/* 应用检查 status，成功后才允许其他任务读写。 */
```

- 初始化借用定义、规则、状态及数据，不复制这些对象；它们须有效至 DeInit。
  定义和状态指针初始化后不变，数据区不得互相重叠。
- 启用定义校验时检查规则和默认值，所有锁创建成功后才写入字段默认值。
  失败时数据保持不变；未登记字段及填充字节保持应用原值。
- item flags 本版为保留字段，必须为零；组 flags 仅支持锁标志。
- Set/Get 的大小必须与组定义完全一致。调用者缓冲区不得与总线数据区重叠。
- `timeout` 为获取互斥锁的等待预算，不包含校验和复制耗时。
- 未初始化返回 `A_STATUS_NOT_READY`，未知 ID 返回 `A_STATUS_NOT_FOUND`，
  越界输入值返回 `A_STATUS_INVALID_PARAM`；调用参数非法触发断言。
- 获取锁失败时不复制；成功获取后的解锁错误会被返回，但复制已发生。
- 生命周期和读写均仅用于启动/任务上下文；接口未实现 ISR 路径。
- 停止全部使用者后调用 `aBusDeInit()`，不释放应用持有的数据。
- 当前尚未实现事件、持久化、原子标量快速路径或读写锁。

主机测试：`python3 tests/bus/run.py`，覆盖三种锁模式；有锁模式验证并发完整快照，无锁模式仅串行访问。


## 7. 通用数值类型与布局

`aScalarType_t` 和 `aScalar_t` 在 aLib 的 `aScalar.h` 中定义，
可由其他模块直接使用，不依赖 aBus。`aBus_ItemDef` 包含偏移和总线校验规则，
继续留在 aBus。

| 类型标签 | 联合体成员 | C 类型 |
| --- | --- | --- |
| `ALIB_SCALAR_U8` | `u8` | `uint8_t` |
| `ALIB_SCALAR_U16` | `u16` | `uint16_t` |
| `ALIB_SCALAR_U32` | `u32` | `uint32_t` |
| `ALIB_SCALAR_I32` | `i32` | `int32_t` |

min、max 和 default_value 必须使用与 type 一致的指定成员初始化。
联合体不携带独立标签，不提供类型转换，运行时无法识别填错成员的配置。
窄整数成员的初始化若发生截断，总线无法恢复原输入，应保留编译器告警检查。

两个 uint16_t 字段相邻，随后排列类型枚举和三个数值联合体。
当前 ARM GCC ABI 下 aBus_ItemDef 为 20 字节，flags 是真实保留字段，
不是可任意写入的编译器填充。布局不作为跨平台协议或持久化格式。


## 8. 检查边界

- 通用类型大小由 aLib 的 `aScalarSize()` 提供，未知类型返回零。
- 组定义将四个 16 位字段放在指针之前，减少指针对齐填充；不使用 packed。
- Get/Set 通过断言检查缓冲区是否与当前组重叠，包括指向同一地址的情况。
  不在每次复制时扫描所有组；调用方仍须避免缓冲区引用其他组的数据。
- 定义表、规则、状态引用等元数据必须与可写数据区分离，且保持有效。
- 任务上下文及生命周期约束由调用方遵守，不提供 ISR 检测或并发销毁。


## 9. 校验与断言配置

```cmake
set(ABUS_DEF_CHECK_ENABLE ON)
```

此开关只控制初始化的 `definitions_check()`，默认开启，关闭后编译移除。
关闭时应用必须保证定义表合法；sigID 唯一性在两种配置下都由应用保证。
默认值是否合法属于定义校验；正常 Set 的输入值范围检查始终执行。

调用契约采用 C 标准 `assert()`，由全局 `NDEBUG` 控制，
没有 aBus 专用参数检查或范围检查开关：

- Init 的配置指针、定义表指针及组数量使用断言。
- Set/Get 的空指针、非法超时、长度不符、当前组缓冲区重叠使用断言。
- 开启断言时，违反契约进入标准断言失败处理，不返回参数错误。
- 定义 `NDEBUG` 后，断言及对应检查函数被移除；调用方违反约定时，
  不保证错误返回或安全执行。测试框架本身仍保持断言开启。
- 未初始化返回 NOT_READY，重复初始化返回 BUSY，未知 sigID 返回 NOT_FOUND。
- 输入值越界始终返回 INVALID_PARAM 且保留旧值。
- 内存分配、锁创建、加锁及解锁的失败处理始终保留。

主机测试组合覆盖三种锁模式、定义校验开关和 NDEBUG 开关。
断言开启时在子进程验证非法调用会触发 SIGABRT；关闭时仅使用合法参数，
避免用未定义行为测试“关闭检查”。范围拒绝及运行错误在全部组合下验证。


## 10. 锁模式与公共结构布局

`ABUS_LOCK_NONE`、`ABUS_LOCK_BUS`、`ABUS_LOCK_SIG` 是公共模式常量，
`ABUS_LOCK_MODE` 由顶层 CMake 配置映射，并通过 aBus 的 PUBLIC 编译定义
传递给使用方。应用目标须链接 aBus，不应只手工添加头文件搜索路径。
独立编译默认为 BUS；手工编译时库与调用方必须使用相同模式并一起重新编译。
SIG 模式公开依赖 aOS 的互斥锁类型，但不暴露具体 OS 后端类型。
组级锁标志仍只控制读写时是否获取锁，不影响顶层模式要求的创建数量。
