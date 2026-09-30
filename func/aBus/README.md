# aBus 板内 RAM 数据总线

## 数据模型

一个 handle 管理一组连续的 aBusTable_t 表，表内每个 SIG 是一次完整发布、读取的数据单元。
SIG 可以保存整数、结构体或字节数组，统一使用连续字节数据区。
定义表可由多个实例共享，各实例的 RAM 数据和锁相互独立。
请求通过 deviceID 选择 handle 内的表；aBus 不设置全局实例或注册表。
同一 handle 内 deviceID 必须唯一，不要求排序；不同表可使用相同 sigIndex、
sigKey。初始化后挂载集合固定，暂不支持运行时增删表。

```text
只读定义                            RAM
 aBusTable_t[]                       aBusHandle_t
  └─ aBusSig_t[]                      └─ aBusSigState_t[]
      └─ aBusParam_t[]（可选）              └─ data → 完整 SIG 快照
```

RAM 状态按表顺序展开为一个数组，表内定义和运行状态按 SIG 下标对应，不为每个参数创建运行对象。
aDataValue_t 只表达范围上下限，不保存运行值。

SIG 支持 U8/U16/U32/S32、RAW 和 STRUCT。标量直接解释整数，RAW 是不解析
的字节块，STRUCT 通过 params 描述字段；不支持参数内嵌套 STRUCT。
SIG 和 Param 共用可选 `const aBusRange_t *range`，NULL 表示没有上下限。
范围仅用于整数，RAW/STRUCT 的 range 必须为 NULL，不需要启用标志位。

params 仅用于 STRUCT，可为空；参数包含 offset、size、type、range。
size 显式指定，整数必须与 aDataTypeSize 一致，RAW 表示字节数。
未登记字段和填充字节仍参加整组复制；允许字段描述重叠，写入必须满足
所有受影响的范围规则。定义、范围对象都应使用 static const。

## 定义示例

```c
typedef struct {
    uint32_t speed;
    uint8_t status;
} MotorData_t;

static const MotorData_t motor_default = {.speed = 100, .status = 1};
static const aBusRange_t speed_range = {
    .min.u32 = 0, .max.u32 = 5000
};
static const aBusParam_t motor_params[] = {
    {
        .offset = offsetof(MotorData_t, speed),
        .type = ALIB_DATA_U32,
        .size = sizeof(uint32_t),
        .range = &speed_range,
    },
};
static const aBusSig_t sigs[] = {
    {
        .sigKey = 10,
        .type = ALIB_DATA_STRUCT,
        .flags = ABUS_SIG_FLAG_LOCK,
        .size = sizeof(MotorData_t),
        .default_data = &motor_default,
        .params = motor_params,
        .param_count = 1,
    },
    {
        .sigKey = 20,
        .type = ALIB_DATA_U32,
        .flags = ABUS_SIG_FLAG_LOCK,
        .size = sizeof(uint32_t),
        /* 无参数描述，初始化为零，写入不做范围校验。 */
    },
};
static const aBusTable_t table = {
    .deviceID = 1,
    .sigs = sigs,
    .sig_count = sizeof(sigs) / sizeof(sigs[0]),
};
```

type 表示整个 SIG 的数据类型，必须显式填写；标量长度必须与类型一致。
STRUCT 的 size 是完整结构体长度，不能用字段长度之和代替。
aDataTypeSize 对 RAW/STRUCT 返回 0，长度由对应描述提供。

aBusGetSigInfo(handle, query, info) 按 deviceID + sigIndex 查询 sigKey、flags、type、size、
range、params、param_count。range/params 是借用的只读描述，生命周期同
handle，禁止修改，不暴露业务数据地址。未就绪返回 NOT_READY，未知目标
返回 NOT_FOUND。定义不可变，查询无需锁。

default_data 非 NULL 时提供 size 字节整组默认数据，否则整组清零。
没有逐字段默认值。定义、规则及默认数据在实例使用期间保持有效且不变。
关闭定义检查后，应用仍须保证初始值符合范围规则。
含指针结构体仅浅拷贝，不管理指向对象；本地快照不是跨平台传输格式。

## 创建与释放

动态创建只需包含 aBus.h：

```c
aBusHandle_t *handle = NULL;
aStatus_t status;

status = aBusCreate(1U, &table, 1U, &handle);
/* 检查 status，成功后才能向使用者发布 handle。 */
```

多表直接传入表数组和数量，表数组生命周期覆盖整个 handle：

```c
static const aBusTable_t tables[] = {
    {.deviceID = 1U, .sigs = sigs, .sig_count = 2U},
    {.deviceID = 2U, .sigs = sigs, .sig_count = 2U}
};

status = aBusCreate(1U, tables, sizeof(tables) / sizeof(tables[0]),
                    &handle);
```

本例共用 Flash SIG 定义，各设备的当前值独立。deviceID 不存在时读写返回
NOT_FOUND。重复 deviceID 在初始化时始终返回 INVALID_PARAM。
BUS 模式整个 handle 共用一把锁，SIG 模式为全部表中的每个 SIG 创建锁。
所有表的资源全部准备成功后才写默认值，任意表失败均整体回滚。

动态创建先用一块内存保存 handle 和 SIG 状态数组，再收集静态绑定，统计
未绑定 SIG 的总字节数，最多再申请一块数据内存。所有 SIG 已绑定时只申请
元数据块。内存申请次数不随 SIG 数量增长，读写期间不申请内存。
数据池按字节切分，内部使用 memcpy，不要求各 SIG 起点类型对齐。

静态创建层包含 aBus_instance.h，只需提供 handle 和 SIG 状态数组：

```c
static aBusHandle_t instance;
static aBusSigState_t states[2];
aStatus_t status;

aBusInstanceStructInit(&instance, states, 2);
status = aBusInitStatic(1U, &table, 1U, &instance);
```

多表静态实例的 states 容量为各表 sig_count 之和。
静态创建使用相同的分散绑定收集流程，任何 SIG 缺少绑定返回 NOT_FOUND。
不再需要总数据池。静态入口不申请数据内存，但 BUS/SIG 模式的 aOSMutexCreate
仍可能动态分配锁；整个模块无堆目前需 NONE 模式或后续静态锁适配。
停止使用后分别通过 aBusDestroy / aBusDeInitStatic 释放锁及实例拥有的内存。
绑定变量从不释放，静态实例可重新初始化，错误释放方式返回 INVALID_PARAM。

## 分散注册静态存储（GCC）

设备号在应用中统一定义，表和绑定变量分别保留在自己的 .c 文件内：

```c
/* motor.c，MOTOR_DEVICE_ID 与私有表的 deviceID 一致。 */
static MotorData_t motor_data;

ABUS_STORAGE_EXPORT(motor_binding, MOTOR_INSTANCE_ID, MOTOR_DEVICE_ID,
                    MOTOR_SIG_CONFIG, motor_data);
```

宏自动记录 instanceID、deviceID、sigIndex、变量地址和 sizeof(variable)，并向链接段导出
一条描述指针。object 必须是实际静态可写对象或数组，不能传缓冲区指针。
deviceID 标识绑定归属；不同设备使用不同 deviceID，可共用 sigs 定义数组。
含绑定的同一 instanceID 只允许一个活动 handle，应用负责生命周期串行化；不设置
全局活动实例注册表。绑定数据和所有元数据、默认数据、其他绑定必须互不重叠。
绑定不保护直接访问，业务自行决定同步策略。

初始化依次执行：检查定义 → 清空状态数组 → 收集匹配表的绑定 → 统计缺口
→ 动态补齐或静态报错 → 创建全部锁 → 写整组默认值/清零 → 发布就绪状态。
绑定使用 sigIndex 直接匹配，重复绑定、越界下标、NULL 地址或容量不足始终
返回 INVALID_PARAM，与定义检查开关无关。累计容量检查溢出。
默认值在全部资源成功后才写入，因此初始化失败不改变绑定变量原内容。
成功初始化会覆盖绑定变量，不保留原业务值；重新初始化同样重置默认值。

当前产品 GD32E505_flash.ld 已使用 KEEP 收集 .abus_bindings 指针段到 Flash。
主机 GCC 使用 port/gcc/aBus_sections_host.ld，段放在重定位只读数据之后，
支持空绑定段。移植其他产品时必须在链接脚本提供相同段边界；不静默忽略绑定。
ARMCC/IAR 尚未适配。

注册源文件须直接加入最终目标或 OBJECT 库。若放在普通 STATIC 库，KEEP
只能保留已提取的对象，不能主动提取未引用对象；只对注册库使用 whole-archive
或 CMake LINK_LIBRARY:WHOLE_ARCHIVE，禁止对全部依赖全局开启。

## 请求结构体与整组读写

接口统一为两个参数：handle 和只读请求指针。读写分别使用请求类型，保留
写入源的 const 约束。请求只在调用期间借用，不被 handle 保存。

```c
MotorData_t motor = {.speed = 200, .status = 2};
MotorData_t snapshot;
aBusSetKeyRequest_t write_request;
aBusGetKeyRequest_t read_request;
aStatus_t status;

aBusSetKeyRequestStructInit(&write_request);
write_request.deviceID = 1U;
write_request.sigKey = 10;
write_request.src = &motor;
write_request.size = sizeof(motor);
write_request.timeout = A_TIMEOUT_MS(10);
status = aBusSetByKey(handle, &write_request);
/* 检查 status。 */

aBusGetKeyRequestStructInit(&read_request);
read_request.deviceID = 1U;
read_request.sigKey = 10;
read_request.dst = &snapshot;
read_request.size = sizeof(snapshot);
read_request.timeout = A_TIMEOUT_MS(10);
status = aBusGetByKey(handle, &read_request);
/* 检查 status 后使用 snapshot。 */
```

两个请求初始化函数默认设置 A_TIMEOUT_NO_WAIT，ID/长度为零、数据指针为 NULL。
调用前填写实际 ID、指针和完整长度；size 使用 size_t。

SetByKey：查找 SIG → 参数断言 → 全部范围检查 → 加锁 → 整组复制 → 解锁。
GetByKey：查找 SIG → 参数断言 → 加锁 → 整组复制 → 解锁。
没有参数描述时范围检查循环不执行。未就绪返回 NOT_READY，未知 ID 返回
NOT_FOUND，超范围返回 INVALID_PARAM。范围或加锁失败不修改当前值，Get 加锁
失败不修改输出。解锁失败时复制已完成，不回滚。timeout 仅表示锁等待预算。

输入输出缓冲区不得与任何总线存储、元数据重叠；Set 源和请求本身在调用期间
保持稳定。每组由一个组装发布入口负责，避免多个模块读旧组再修改写回而覆盖。

## 同步与编译配置

在 config/aclass_config.cmake 中设置：

```cmake
set(ABUS_ENABLE ON)
set(ABUS_STATIC_ENABLE ON)
set(ABUS_DYNAMIC_ENABLE ON)
set(ABUS_LOCK_GRANULARITY BUS)
set(ABUS_DEF_CHECK_ENABLE ON)
```

| 模式 | 创建资源 | 使用规则 |
| --- | --- | --- |
| NONE | 无锁 | 应用负责串行化或外部同步 |
| BUS | 每个 handle 一把锁 | 标记 ABUS_SIG_FLAG_LOCK 的 SIG 使用 |
| SIG | 每个 SIG 一把锁 | 标记 ABUS_SIG_FLAG_LOCK 的 SIG 使用 |

锁成员及操作按 #if 裁剪；组标志不影响锁创建数量。
未使用锁的 SIG 也要求应用保证无并发冲突，允许半更新值不能消除 C 数据竞争。
锁通过 aOS 实现，只在任务/启动上下文使用，不支持 ISR。
静态/动态创建至少启用一种，配置影响公开布局，使用方应链接 aBus 继承配置。

定义检查开关控制类型、边界、范围及默认值检查；关闭后应用负责合法定义。
调用参数通过标准 assert 检查，NDEBUG 可移除。写入范围校验没有关闭开关。
内存/锁失败、静态容量和释放所有权始终处理。
sigKey 简单线性查找，同表唯一性由应用保证，不进行嵌套重复 ID 扫描。

## 扩展边界

只支持整组连续变量绑定，不支持字段分散绑定、运行中重新绑定或保留绑定原值。
不实现事件、版本、有效标志、持久化或转发。

## 验证

运行 python3 tests/bus/run.py，覆盖三种锁模式、定义检查/断言开关及三种
分配配置，共 36 组。包括并发快照、整组默认值、无规则数据、部分字段规则、
非对齐字段、多表/多实例隔离、重复设备号、跨表失败回滚、资源失败回滚、跨翻译单元链接段收集、重复/缺失/短容量绑定和错误释放方式。
SANITIZE=1 启用 ASan/UBSan。

## 下标与稳定键

sigIndex 是数组下标，不在 aBusSig_t 内重复存储；业务使用枚举配合指定下标
初始化定义表。sigKey 是同表唯一的稳定业务键，无需有序或连续，不复用已发布
含义，可用于通信和持久化。多张表共享存储空间时还需稳定的表标识。
持久化数据格式版本与 sigKey 分开管理。本模块不实现持久化或重复键扫描，
唯一性由应用/生成工具保证。

按下标调用 aBusSetByIndex / aBusGetByIndex，使用对应的
 aBusSetIndexRequest_t / aBusGetIndexRequest_t。请求字段为 deviceID、sigIndex、
src/dst、size、timeout，各有 RequestStructInit 初始化函数，默认不等待锁。
例如：

```c
aBusGetIndexRequest_t request;

aBusGetIndexRequestStructInit(&request);
request.deviceID = MOTOR_DEVICE_ID;
request.sigIndex = MOTOR_SIG_CONFIG; /* 应用定义的表内枚举。 */
request.dst = &snapshot;
request.size = sizeof(snapshot);
status = aBusGetByIndex(handle, &request);
```

先按 deviceID 查找表 O(T)，表内 Index 定位 O(1)；Key 查找 O(N)。
Key 找到后调用 Index 入口，范围校验、锁和复制
语义完全相同。越界下标和未知键返回 NOT_FOUND，未初始化实例返回 NOT_READY。
两类请求不混用定位字段。旧的 aBusSetSig / aBusGetSig 接口已移除。

## STRUCT 参数读写与 Shell

`aBusSetParam` / `aBusGetParam` 使用独立请求结构体，包含 deviceID、sigIndex、
paramIndex、src/dst、size、timeout；各有 StructInit，默认 NO_WAIT。
应用通过 appSigSetParam/appSigGetParam 转发，不公开 handle。

字段读写使用父 SIG 的锁配置；写入在锁内校验受影响字段的范围，然后只复制
目标字节。不会先读取整个结构体再写回，因此不覆盖其他字段的新值。
若描述有重叠，也检查重叠字段的范围；失败不修改数据。不使用动态内存。
未知参数返回 NOT_FOUND，非 STRUCT 返回 UNSUPPORTED，长度错误返回
INVALID_PARAM。无锁或应用直接访问绑定变量时，同步仍由应用决定。

Shell 不维护独立的类型表，全部根据查询到的定义进行解析：

```text
sig get <deviceID> [sigIndex [paramIndex]]
sig set <deviceID> <sigIndex> [paramIndex] <value>
```

- 标量使用十进制，先检查类型表示范围，再由 aBus 检查可选业务范围。
- RAW 使用连续十六进制字符串，不加 0x，两个字符对应一字节；大小写均可，
  写入字节数必须完全匹配。显示为大写十六进制。
- STRUCT 整组读取一次快照，再显示全部登记参数；写入必须指定参数下标。
  不支持整组 STRUCT 文本赋值和递归组合。

Shell 临时使用 aOSAlloc 申请目标数据长度的缓冲区，每条命令结束后释放。
这与 aBus 的静态/动态实例开关独立；申请失败返回错误，不修改数据。
RAW 是平台内存字节序，包含应用定义的填充字节，不是通信序列化格式。

额外运行 `python3 tests/bus/test_app_sig.py` 和
`python3 tests/bus/test_shell_types.py`，分别检查真实产品绑定/任务和独立点表的
标量、RAW、STRUCT 命令，覆盖非法输入、范围拒绝及内存失败。

### 读取整个设备

```text
sig get 1
```

省略 sigIndex 时按下标顺序显示设备 1 的全部 SIG，STRUCT 展开登记字段，
RAW 显示十六进制。保留 `sig get 1 0`（整个 Motor）和
`sig get 1 0 0`（Motor.speed）的用法。
每个 SIG 单独读取快照，整张表不保证来自同一时刻；读取失败时停止并报告错误。

## 实例归属与链接段

创建接口第一个参数为 instanceID，匹配静态绑定的实例归属：

```c
ABUS_STORAGE_EXPORT(counter_binding, 1U, 7U, COUNTER_INDEX, counter);
/* 实例 1 中设备 7 的 Counter 绑定到 counter。 */
status = aBusCreate(1U, tables, table_count, &handle);
```

不同 handle 使用不同 instanceID 后，即使 deviceID、sigIndex 完全一致，也
不会收集到彼此的绑定。instanceID 属于 handle，不放进 aBusTable_t，允许
多实例共用同一份 Flash 定义。读写已经传入 handle，无需再传 instanceID。
实例号 0 也有效；含绑定实例的标识唯一性由应用保证，不维护全局活动句柄表。

链接段继续统一使用 `.abus_bindings`。初始化时先过滤 instanceID，再按
设备号和下标收集；无关实例的绑定不参与校验。不需要按实例划分链接段，
读写期间不扫描注册段。未匹配的存储在静态模式返回 NOT_FOUND，在动态模式
按原规则分配内存补齐。链接脚本无需随 handle 数量变化。
