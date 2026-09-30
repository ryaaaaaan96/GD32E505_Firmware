# aBus 板内 RAM 数据总线

## 数据模型

一个 handle 管理一张表，表内每个 SIG 是一次完整发布、读取的数据单元。
SIG 可以保存整数、结构体或字节数组，统一使用连续字节数据区。
定义表可由多个实例共享，各实例的 RAM 数据和锁相互独立。
应用根据 deviceID 选择 handle；aBus 不设置全局实例或注册表。

```text
只读定义                            RAM
 aBusTable_t                         aBusHandle_t
  └─ aBusSig_t[]                      └─ aBusSigState_t[]
      └─ aBusParam_t[]（可选）              └─ data → 完整 SIG 快照
```

定义和运行状态按 SIG 数组下标对应，不为每个参数创建运行对象。
aDataValue_t 只表达范围上下限，不保存运行值。

多数 SIG 不需要参数描述，params 为 NULL、param_count 为零即可。
少部分字段需要校验时，登记 offset、type、min、max；其余字节仍整组复制。
参数支持 U8/U16/U32/S32，长度从类型推导，不支持 RAW 范围规则。
允许多条范围规则描述重叠字节，写入必须满足全部规则。

## 定义示例

```c
typedef struct {
    uint32_t speed;
    uint8_t status;
} MotorData_t;

static const MotorData_t motor_default = {.speed = 100, .status = 1};
static const aBusParam_t motor_params[] = {
    {
        .offset = offsetof(MotorData_t, speed),
        .type = ALIB_DATA_U32,
        .min.u32 = 0,
        .max.u32 = 5000,
    },
};
static const aBusSig_t sigs[] = {
    {
        .sigKey = 10,
        .flags = ABUS_SIG_FLAG_LOCK,
        .size = sizeof(MotorData_t),
        .default_data = &motor_default,
        .params = motor_params,
        .param_count = 1,
    },
    {
        .sigKey = 20,
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

default_data 非 NULL 时提供 size 字节整组默认数据，否则整组清零。
没有逐字段默认值。定义、规则及默认数据在实例使用期间保持有效且不变。
关闭定义检查后，应用仍须保证初始值符合范围规则。
含指针结构体仅浅拷贝，不管理指向对象；本地快照不是跨平台传输格式。

## 创建与释放

动态创建只需包含 aBus.h：

```c
aBusHandle_t *handle = NULL;
aStatus_t status;

status = aBusCreate(&table, &handle);
/* 检查 status，成功后才能向使用者发布 handle。 */
```

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
status = aBusInitStatic(&table, &instance);
```

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

ABUS_STORAGE_EXPORT(motor_binding, MOTOR_DEVICE_ID,
                    MOTOR_SIG_CONFIG, motor_data);
```

宏自动记录 deviceID、sigIndex、变量地址和 sizeof(variable)，并向链接段导出
一条描述指针。object 必须是实际静态可写对象或数组，不能传缓冲区指针。
deviceID 标识绑定归属；不同设备使用不同 deviceID，可共用 sigs 定义数组。
含绑定的同一 deviceID 只允许一个活动 handle，应用负责生命周期串行化；不设置
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
write_request.sigKey = 10;
write_request.src = &motor;
write_request.size = sizeof(motor);
write_request.timeout = A_TIMEOUT_MS(10);
status = aBusSetByKey(handle, &write_request);
/* 检查 status。 */

aBusGetKeyRequestStructInit(&read_request);
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
不实现事件、版本、有效标志、持久化、转发或参数级读写接口。

## 验证

运行 python3 tests/bus/run.py，覆盖三种锁模式、定义检查/断言开关及三种
分配配置，共 36 组。包括并发快照、整组默认值、无规则数据、部分字段规则、
非对齐字段、多实例隔离、资源失败回滚、跨翻译单元链接段收集、重复/缺失/短容量绑定和错误释放方式。
SANITIZE=1 启用 ASan/UBSan。

## 下标与稳定键

sigIndex 是数组下标，不在 aBusSig_t 内重复存储；业务使用枚举配合指定下标
初始化定义表。sigKey 是同表唯一的稳定业务键，无需有序或连续，不复用已发布
含义，可用于通信和持久化。多张表共享存储空间时还需稳定的表标识。
持久化数据格式版本与 sigKey 分开管理。本模块不实现持久化或重复键扫描，
唯一性由应用/生成工具保证。

按下标调用 aBusSetByIndex / aBusGetByIndex，使用对应的
 aBusSetIndexRequest_t / aBusGetIndexRequest_t。请求字段为 sigIndex、
src/dst、size、timeout，各有 RequestStructInit 初始化函数，默认不等待锁。
例如：

```c
aBusGetIndexRequest_t request;

aBusGetIndexRequestStructInit(&request);
request.sigIndex = MOTOR_SIG_CONFIG; /* 应用定义的表内枚举。 */
request.dst = &snapshot;
request.size = sizeof(snapshot);
status = aBusGetByIndex(handle, &request);
```

Index 定位 O(1)；Key 查找 O(N)，找到后调用 Index 入口，范围校验、锁和复制
语义完全相同。越界下标和未知键返回 NOT_FOUND，未初始化实例返回 NOT_READY。
两类请求不混用定位字段。旧的 aBusSetSig / aBusGetSig 接口已移除。
