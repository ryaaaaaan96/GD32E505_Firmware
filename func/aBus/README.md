# aBus 板内 RAM 数据总线

事件 API 由 `aBus.h` 引入 `aBus_event.h`。完整的跨模块边界和请求生命周期见
[发布订阅设计](../../docs/abus_protocol_events_design.md)。
`ABUS_EVENT_ENABLE` 控制编译，默认开启。

## 数据模型

`aBus_table.h` 提供只读定义，独立于运行库配置和链接段收集；仅借用点表的
模块链接 `aBusTable` 即可。读写及绑定接口仍在 `aBus.h`，链接 `aBus`。
原有包含 `aBus.h` 的业务代码无需改动。

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

RAM 状态按表顺序展开为一个数组，定义与状态按 SIG 下标对应。
事件开启时，SIG 与已声明 Param 各有链表头；Param 状态不重复保存值。
aDataValue_t 只表达范围上下限，不保存运行值。
初始化的容量统计、地址分配与默认值写入按表顺序推进，避免每个 SIG
重新从首表查找定义；这些遍历为 O(SIG 总数 + 表数)，不新增索引 RAM。
设备号去重仍为 O(表数平方)，绑定收集仍逐项查找设备，定义范围检查另计。

SIG 支持 U8/U16/U32/S32、RAW 和 STRUCT。标量直接解释整数，RAW 是不解析
的字节块，STRUCT 通过 params 描述字段；不支持参数内嵌套 STRUCT。
SIG 和 Param 共用可选 `const aBusRange_t *range`，NULL 表示没有上下限。
范围仅用于整数，RAW/STRUCT 的 range 必须为 NULL，不需要启用标志位。

params 仅用于 STRUCT，可为空；参数包含 offset、size、type、range。
size 显式指定，整数必须与 aDataTypeSize 一致，RAW 表示字节数。
未登记字段和填充字节仍参加整组复制；允许字段描述重叠，写入必须满足
所有受影响的范围规则。定义、范围对象都应使用 static const。

已知 STRUCT 字段互不重叠时，可设置 ABUS_SIG_FLAG_DISJOINT。
定义检查启用时在初始化验证此约束；随后单字段写入只检查该字段的范围。
不设置此标志仍兼容重叠字段，并检查所有受影响规则。
关闭定义检查时，由配置者保证此约束成立。


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

## 可选参数长度检查

`aBusSigParamSizeCheck(const aBusSig_t *sig)` 在 `aBus_table.h` 提供，应用可以
在初始化前主动调用，检查 STRUCT 的 `size` 是否等于全部 `param.size` 的和。
`sig->size` 应填写真实结构体的 `sizeof`，字段大小填写成员的 `sizeof`。

```c
/* config_sig 为应用持有的 STRUCT 定义，params 描述保持有效。 */
aStatus_t status = aBusSigParamSizeCheck(&config_sig);
if (status != A_STATUS_OK) {
    return status;
}
```

函数不需要 handle、不访问业务数据、不加锁、不分配内存，只需借用只读定义。
耗时 O(参数数)，累计前检查剩余长度以避免溢出。返回值如下：

| 返回值 | 含义 |
| --- | --- |
| `A_STATUS_OK` | 所有非零字段长度的总和等于 SIG 大小 |
| `A_STATUS_UNSUPPORTED` | SIG 不是 STRUCT |
| `A_STATUS_INVALID_PARAM` | 空指针、空参数表、零长度或总长度不等 |

由用户决定是否调用；初始化、Set/Get 不自动调用，不增加强制配置开关。
显式调用不受 `ABUS_DEF_CHECK_ENABLE` 或 `NDEBUG` 影响。允许只描述部分字段
或带填充的普通 aBus SIG 继续使用原有初始化方式。

这只是长度求和，不检查 offset、字段重叠、类型、上下限或偶数字节要求。
例如重复登记一个字段、遗漏另一个等长字段，仍可能返回 OK。因此通过不代表
布局连续，也不代表可以直接映射为 Modbus 寄存器。协议使用方仍需检查自身
依赖的边界和布局规则；Modbus 整组映射额外要求字段连续且为偶数字节。

如果用户希望在编译时检查结构体与成员大小之和，可以使用
[aLib 布局断言](../../platform/aLib/README.md#可选布局断言)。普通 C 的 const
参数数组不能用于编译期断言。主机检查：`python3 tests/layout/run.py`。

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

静态创建层包含 aBus_instance.h，提供 handle、SIG 及可选 Param 状态数组：

```c
static aBusHandle_t instance;
static aBusSigState_t states[2];
#if ABUS_EVENT_ENABLE
/* 所有挂载 SIG 的 param_count 之和；本例 Motor 有两个字段。 */
static aBusParamState_t params[2];
#endif
aStatus_t status;

aBusInstanceStructInit(&instance, states, 2);
#if ABUS_EVENT_ENABLE
instance.params = params;
instance.param_capacity = 2U;
#endif
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

ABUS_RAM_BIND_EXPORT(motor_binding, MOTOR_INSTANCE_ID, MOTOR_DEVICE_ID,
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
不自动实现版本、有效标志、持久化或远端转发。事件本身不携带历史值。

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
唯一性由应用/生成工具保证。当前产品构建使用 scripts/check_sig.py 校验清单。

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
`aBusResolveKey` 可将 deviceID + sigKey 解析为当前实例的 sigIndex；
不加锁、不分配内存，失败不修改输出，结果不跨实例或版本保存。
Key 找到后调用 Index 入口，范围校验、锁和复制
语义完全相同。越界下标和未知键返回 NOT_FOUND，未初始化实例返回 NOT_READY。
两类请求不混用定位字段。旧的 aBusSetSig / aBusGetSig 接口已移除。

## STRUCT 参数读写与 Shell

`aBusSetParam` / `aBusGetParam` 使用独立请求结构体，包含 deviceID、sigIndex、
paramIndex、src/dst、size、timeout；各有 StructInit，默认 NO_WAIT。
应用通过 dataBusSetParam/dataBusGetParam 转发，不公开 handle。

字段读写使用父 SIG 的锁配置；写入在锁内校验受影响字段的范围，然后只复制
目标字节。不会先读取整个结构体再写回，因此不覆盖其他字段的新值。
若描述有重叠，也检查重叠字段的范围；失败不修改数据。不使用动态内存。
未知参数返回 NOT_FOUND，非 STRUCT 返回 UNSUPPORTED，长度错误返回
INVALID_PARAM。无锁或应用直接访问绑定变量时，同步仍由应用决定。

应用的 `task/system/command_data_bus.c` 不维护独立类型表，先解析稳定键，
再根据查询到的定义进行解析：

```text
sig get <deviceID> [sigKey [paramIndex]]
sig set <deviceID> <sigKey> [paramIndex] <value>
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
标量、RAW、STRUCT 命令，覆盖不同表顺序、跨设备同键、非法输入、
范围拒绝及内存失败。Shell 使用 sigKey，STRUCT 字段仍使用 paramIndex。

### 读取整个设备

```text
sig get 1
```

省略 sigKey 时按下标顺序显示设备 1 的全部 SIG，STRUCT 展开登记字段，
RAW 显示十六进制。当前产品用 `sig get 2 2001` 查看组合 TEST，
用 `sig get 2 2001 0` 查看其第一个字段。
每个 SIG 单独读取快照，整张表不保证来自同一时刻；读取失败时停止并报告错误。

## 实例归属与链接段

创建接口第一个参数为 instanceID，匹配静态绑定的实例归属：

```c
ABUS_RAM_BIND_EXPORT(counter_binding, 1U, 7U, COUNTER_INDEX, counter);
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

## 在同一临界区修改多个字段

aBusUpdateSig 使用请求中的调用者工作区，读取完整 SIG 快照，调用 edit，
校验整个结果后一次提交。回调错误、范围错误或锁等待失败不会修改目标值；
解锁失败不回滚已经提交的数据。工作区大小至少等于完整 SIG，且不得与目标
存储重叠。若回调用结构体指针访问，调用者还须保证工作区的相应对齐。

锁策略仍遵循 ABUS_LOCK_MODE 和 ABUS_SIG_FLAG_LOCK。回调在持锁期间运行，
只做有界内存操作，不调用 I/O、不阻塞、不重入总线。此接口只对一个 SIG
提供一致修改，跨 SIG 不提供事务；无锁和直接绑定变量访问由应用同步。

## 非阻塞事件与请求

成功 Set/SetParam/SetKey/Update 在释放数据锁后自动发布一次 UPDATED。
通知失败不回滚提交，用 `aBusNotificationFailed` 取出并清除故障标记。
直接修改外部绑定变量不自动通知，需要应用显式 Publish。

`aBusSubscribe` 挂载应用持有的节点；真实事件需要回调，MARK_ONLY 只供反查。
Subscribe 的零掩码是参数错误；协议配置零掩码则跳过登记。
`aBusAssociationGet/Find` 枚举或要求唯一关联，不遍历未登记的协议配置。

`aBusPublish` 发布普通通知，`aBusRequest` 投递 READ/WRITE 请求。
按字节交集通知整组及字段；默认多播，可按 owner 筛选。无人处理直接丢弃，
接收统计为零；队列满返回 BUSY，但其他接收者可能已经接收。

请求预先提供 `aBusRequest_t` 与 `aBusDelivery_t[]`，消费者完成时调用
`aBusComplete`。只有 `aBusRequestDone` 为真才复用请求及数据；分发期间的
提前完成不会提前释放请求。每个已接收节点完成一次，结果不合并。

发布/接收/完成回调只登记工作，不等待锁、I/O、完成或队列空间，不申请堆。
提供者只能尝试入队，必要时合并 pending 标记。仅任务上下文，不支持热插拔。
销毁前停止调用者、排空请求并解除节点；仍有关联或借用时返回 BUSY。
主机验证：`python3 tests/bus_events/run.py`。


## 可选通信端点

启用 ACOMM_ENABLE 时编译 aBus_endpoint.c。应用用 aBusEndpointInit 借用已就绪
的总线，再将首成员 base 交给 aComm 映射。总线核心不调用通信管理或协议栈。
端点提供 SIG/字段读写、元信息、快照更新与可选事件，不额外创建锁或分配内存。
同步与范围校验沿用总线原有接口；销毁映射不会销毁总线。
静态订阅的 native 存储使用 aBusEndpointSubscription_t，生命周期覆盖映射。
