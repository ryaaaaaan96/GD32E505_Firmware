# aDataBase

封装官方 FlashDB，提供 KV 参数存储和 TSDB 时序存储。业务头文件
[aDataBase.h](aDataBase.h) 仅使用项目类型，不暴露 FlashDB 或 SPI 类型。
官方源码位于 `FlashDB/`，提交为
`db0afd954ea0f11397e43f6baf3654979d446345`，在此基础上增加 aMemory
存储适配补丁及可选 KV 索引补丁；TSDB 算法源文件保持原样。

```text
应用 KV / TSDB
    → aDataBase → FlashDB → aMemory → 应用设备回调
    → Flash25q → SFUD → SPI 移植层 → aDrvSpi
```

## 文件职责

| 文件或目录 | 职责 |
| --- | --- |
| aDataBase.h / aDataBase.c | 公共请求、生命周期、KV 和 TSDB 操作 |
| aDataBase_index.c | SIG 持久化清单、稀疏索引、启动重建及直接映射 |
| aDataBase_instance.h | 静态分配所需的私有实例布局 |
| aDataBase_internal.h | 模块内部的事务和实例管理声明 |
| config/fdb_cfg.h | 项目 FlashDB 配置，开启 KV、TSDB、64 位时间戳 |
| config/fdb_memory_port.h | 向存储补丁提供总预算和首错记录 |
| port/memory_port.c | 模块锁、事务状态及 aMemory 生命周期引用 |
| patches/0001-amemory.patch | 四个上游文件的可重放存储适配补丁 |
| patches/0002-kv-index.patch | KV 查找、提交及 GC 索引通知和完整扇区缓存 |
| FlashDB | 编译 KV/TSDB 核心，不编译 FAL、示例或文件模式 |

## 对象和初始化

aMemory 支持注册多个设备和分区，每个分区最多有一个活动数据库实例。
数据库按分区名选取存储，不绑定唯一全局介质。
KV 和 TSDB 使用不同句柄，可以同时存在。句柄既可由应用静态提供，
也可通过 Create 动态创建；不拥有底层 Flash。

1. 应用初始化设备并注册到 aMemory，然后调用 aDataBaseInit。
2. 对配置调用 StructInit，补齐 name 和 partition。
3. 调用 KvInitStatic / KvCreate 或 TsInitStatic / TsCreate。
4. 使用请求结构体调用读写接口。
5. 停止使用者，关闭全部数据库，再调用 aDataBaseDeInit、
   aMemoryDeInit，最后销毁底层设备。

静态实例需要包含 aDataBase_instance.h；该头文件的布局不是稳定 ABI。
配置 name 借用至实例关闭；请求和数据仅在本次调用期间借用。
全部生命周期必须由应用串行编排，不得与业务操作并发。
存在活动实例时 DeInit 返回 BUSY，同一分区重复创建返回 BUSY。
aDataBaseInit 至 DeInit 期间持有 aMemory 引用，阻止存储提前反初始化。

默认 format_if_needed 为 A_FALSE：未格式化或扇区头无效时返回 NOT_READY，
不会因此自动擦除。打开已有数据库仍可能进行官方的未完成事务恢复。
显式设为 A_TRUE 才允许官方初始化修复或格式化无效扇区；不等同于
每次都清空已有数据库。失败不能保证回滚已提交的写入或擦除。

## KV

| 接口 | 语义 |
| --- | --- |
| aDataBaseKvSet | 按 key 完整替换二进制值，不把数据限定为字符串 |
| aDataBaseKvGet | 查询长度或读取完整值，不截断 |
| aDataBaseKvDelete | 删除 key；未知 key 返回 NOT_FOUND |

key 大小写敏感，长度为 1 到 63 字节。写入长度必须大于零，单条值最多为
擦除块大小减 128 字节；为官方元数据及最长 key 保守预留空间。
Get 的 size_out 必填，data 为 NULL 且 capacity 为零时仅查询长度。
容量不足返回 NO_MEMORY，并报告所需长度，不写入部分输出。
模块使用官方二进制接口，不使用其共享字符串缓冲区。

```c
aDataBaseKvSetRequest_t request;
aDataBaseKvSetRequestStructInit(&request);
request.key = "motor.speed";
request.data = &speed;
request.size = sizeof(speed);
status = aDataBaseKvSet(handle, &request);
```

## aBus 参数持久化索引

数据库只借用 aBus 的只读定义，不需要持有或创建 aBus RAM handle。
应用决定何时取快照、保存及恢复；模块不会在 aBus Set 时自动写 Flash。
Set/Get 使用完整字节快照，不校验业务范围，也不改变业务 RAM 的同步规则。
当前应用的 `db` 字符串 KV 命令保持原有用法；应用点表尚未自动接入保存。

### 三种标识

| 标识 | 用途 | 是否需要跨固件保持稳定 |
| --- | --- | --- |
| sigIndex | 当前 aBus 表内位置，业务请求定位 | 否 |
| deviceID + sigKey | 外部 Flash 中识别同一 SIG | 是 |
| 持久化槽位 | 紧凑 RAM 偏移数组下标 | 否，每次打开重新构建 |

FlashDB 键为 `@sig:DDDD:KKKK`，D/K 各四位大写十六进制。
不同数据库分区天然隔离；同一数据库内 deviceID 必须唯一。
多个 aBus 实例复用相同 deviceID 时，应使用不同数据库分区或重新规划设备号。
`name` 仅为运行实例名，不是键的命名空间。前缀 `@sig:` 保留给本接口。

`persist_sigs` 是独立 const 清单，按 deviceID、sigKey 升序且不重复；
只列出要保存的 SIG。偏移数组每项 4 字节，不随完整 aBus 表项数增长。
初始化检查清单排序、键唯一性、定义匹配、长度和静态缓存容量。
未列入清单的 SIG 返回 UNSUPPORTED；已列入但尚未保存返回 NOT_FOUND。
清单删去某项不会自动删除外部 Flash 的旧记录。

可选 `sig_slots` 放在程序 Flash，直接按 sigIndex 找到 RAM 槽位：

```text
正常访问：deviceID → 表 → sig_slots[sigIndex] → RAM 记录偏移
启动恢复：FlashDB 记录的 deviceID + sigKey → 持久化清单 → RAM 记录偏移
```

设备定位为 O(表数)，配置直接映射后表内定位为 O(1)，不再按 sigKey 搜索。
不提供某张表的映射时，使用清单二分查找 O(log P)，P 为持久化项数。
这两种方式共用同一份 RAM 索引和同一种外部 Flash 格式，可以按表选择。
直接映射使用 uint16_t，UINT16_MAX 表示不保存，实际槽位编号必须更小。
外层映射指针数组与 tables 顺序一致，内层长度必须等于对应 sig_count；
初始化检查每个映射是否对应清单中的正确稳定键。

例如 1000 个 SIG 中只保存 50 个：RAM 偏移为 200 字节；完整直接映射
另用约 2000 字节程序 Flash。二分方案省去这张映射，保留 200 字节 RAM。
两种方式还需要完整扇区缓存及固定实例状态；这些未计入上述偏移大小。
外部 Flash 记录位置会随写入和 GC 改变，位置只能保存在可更新的 RAM 中。

### 配置示例

假设 `bus_tables` 只有一张设备 1 的表，SIG 顺序为 Motor、Counter，
只保存 Counter，其稳定键为 20。表及所有引用数组在关闭前保持有效且不变：

```c
static const aDataBaseSigKey_t persistent[] = {
    { .deviceID = 1U, .sigKey = 20U }
};
static const uint16_t slots[] = {
    ADATABASE_SIG_SLOT_NONE, 0U
};
static const uint16_t *const table_slots[] = { slots };

aDataBaseKvConfig_t config;
aDataBaseKvConfigStructInit(&config);
config.name = "parameters";
config.partition = "param";
config.tables = bus_tables;
config.table_count = 1U;
config.persist_sigs = persistent;
config.persist_count = 1U;
config.sig_slots = table_slots;
/* 动态入口为缓存一次申请整块内存；不逐项分配。 */
status = aDataBaseKvCreate(&config, &handle);
```

纯静态入口额外提供下面的缓存。本例分区 128 KiB、擦除块 4 KiB：

```c
#include "aDataBase_instance.h"

static aDataBaseKvHandle_t instance;
static uint32_t sig_offsets[1];
static struct kvdb_sec_info sectors[32];
static aDataBaseKvIndexStorage_t index_storage = {
    .sig_offsets = sig_offsets,
    .sig_capacity = 1U,
    .sectors = sectors,
    .sector_capacity = 32U
};

config.index_storage = &index_storage;
status = aDataBaseKvInitStatic(&config, &instance);
```

静态接口不为索引分配堆内存；模块锁仍由 aOS 创建。
动态接口也可借用调用者提供的 index_storage，关闭时不释放借用数组。
缓存不能在活动实例间共用。旧的纯静态字符串 KV 实例若不配置清单和缓存，
继续使用官方小缓存；动态实例默认分配完整扇区缓存。

保存及恢复由应用显式调用：

```c
aDataBaseSigSetRequest_t save;
aDataBaseSigSetRequestStructInit(&save);
save.deviceID = 1U;
save.sigIndex = 1U; /* 业务代码应使用点表枚举。 */
save.data = &counter_snapshot;
save.size = sizeof(counter_snapshot);
status = aDataBaseSigSet(handle, &save);
```

Get/ Delete 分别使用 `aDataBaseSigGetRequest_t`、
`aDataBaseSigDeleteRequest_t` 及对应 StructInit。
Get 长度必须同时匹配当前 SIG 定义和已存记录，长度不符返回 INVALID_PARAM，
不会截断或覆盖输出。稳定键只解决身份对应；结构体字段布局、字节序、
类型变化及默认值恢复策略仍由应用处理。

### 一致性与开销

打开数据库先执行官方恢复，再预热扇区写入位置、扫描记录并构建 RAM 索引。
不存在的项也有明确状态，不会在每次首次保存时遍历历史记录。
索引接入 FlashDB 内部查找；普通字符串接口操作同一个 SIG 键也会同步索引。
仅在新记录提交成功后更新偏移；删除旧记录时核对旧地址，避免清除新地址。
GC 搬迁同步修改偏移，扇区擦除后对应缓存失效。
I/O 或索引记录 CRC 错误后实例停止业务访问，关闭重开完成恢复及重建。

正常更新仍有当前记录 CRC 读取、Flash 编程和 RAM 扇区统计；并非零读取，
也不承诺恒定写延迟。完整扇区缓存消除正常分配时的反复扇区 Flash 读取，
分配逻辑仍为 O(扇区数) 的 RAM 遍历。启动及 GC 仍需遍历记录和擦除扇区。
TSDB 不使用该索引，保持原有追加指针机制。

## TSDB

追加请求携带正数的 64 位时间戳，必须严格大于上一条成功记录。
时间单位由应用统一，可使用 RTC 时间或逻辑序号；模块不自动获取 RTC，
也不把复位归零的系统运行时间当成掉电后连续的时钟。

TsAppend 存储任意二进制记录。max_record_size 默认为 256 字节，配置上限
为擦除块减 128 字节；rollover 默认开启，满后按扇区覆盖最旧记录。
关闭覆盖时，封装额外检查下一扇区，防止官方地址未回绕时擦除已有记录。
接近扇区尾部时保守预留索引空间，最多提前预留 128 字节。

TsIterate 按包含起止点的时间范围升序读取，使用调用者提供的复用缓冲区。
仅返回已提交且未删除的普通记录；缓冲不足返回 NO_MEMORY，不截断记录。
回调在调用任务中、持有模块锁时执行：返回 A_TRUE 继续，A_FALSE 提前停止。
记录数据仅在本次回调期间有效。回调不得重入数据库或修改其生命周期。
TsGetInfo 查询最近时间戳、最大记录长度和覆盖开关。

```c
aDataBaseTsAppendRequest_t request;
aDataBaseTsAppendRequestStructInit(&request);
request.timestamp = sample_timestamp;
request.data = &sample;
request.size = sizeof(sample);
status = aDataBaseTsAppend(handle, &request);
```

## 同步和错误

模块锁覆盖整个 FlashDB 操作，包括初始化、查询、复制和迭代回调。
底层顺序为数据库模块锁、SFUD 模块锁、SPI 总线锁。官方的 void 锁回调
无法报告超时，因此本封装在进入官方接口前检查锁结果。

一次操作在等待模块锁前计算截止时间，所有底层回调使用剩余预算。
不支持 NO_WAIT；支持正数毫秒和 FOREVER。首个存储错误优先返回，
后续官方回调停止访问介质，避免上游修复路径掩盖超时或 I/O 错误。
发生存储错误的实例拒绝继续业务访问，应用关闭并重新打开后恢复缓存。

禁止绕过 aDataBase 封装调用 FlashDB，否则无法满足锁、错误和总预算契约。
数据库调用设备回调时持有模块锁，设备回调不得再次调用数据库接口。
数据库存续期间，其他使用者不得直接擦写其分区，避免破坏数据及内部缓存。

## 构建和验证

ADATABASE_ENABLE 控制整个模块，依赖 AMEMORY_ENABLE。
STATIC_ENABLE / DYNAMIC_ENABLE 控制创建接口，至少启用一种。
不再提供 ADATABASE_BACKEND、ADATABASE_LAYOUT_FILE 或 Flash25q 适配 target。
产品布局由应用注册到 aMemory，数据库库目标不读取产品布局头文件。

FlashDB 使用新增的 FDB_USING_AMEMORY_MODE，底层入口直接调用
`aMemoryRead/Write/Erase`，不链接或调用 FAL。默认 FDB_WRITE_GRAN=1；
初始化检查可读写擦除、擦除值 0xFF、编程粒度匹配及分区长度等条件，
不支持的介质会在访问存储前拒绝打开。aMemory 的地址可以超过 4 GiB，
但当前 FlashDB 分区内地址仍为 32 位，分区容量必须不超过 UINT32_MAX。

FlashDB 核心作为 OBJECT target 合入 aDataBase，与项目代码使用相同的
编译参数，含 Wall、Wextra、Wpedantic、Werror。

修改分别保存为 aMemory 和 KV 索引两个补丁。重新下载上述基线版本后，
在项目根目录依次执行一次，无需在 FlashDB 目录保留独立 Git 仓库：

```sh
patch -d func/aDataBase/FlashDB -p1 < \
    func/aDataBase/patches/0001-amemory.patch
patch -d func/aDataBase/FlashDB -p1 < \
    func/aDataBase/patches/0002-kv-index.patch
```

已打补丁的目录不要重复应用。升级上游版本时需重新检查补丁和测试，
构建脚本检查两个扩展是否存在，不会自动修改上游目录。

```sh
SANITIZE=1 python3 tests/database/run.py
python3 tests/database/test_flash_chain.py
python3 tests/database/build_matrix.py
cmake --build build/Debug -j 4
```

测试使用实际官方源码，覆盖静态/动态分配、空白介质保护、垃圾回收、
TSDB 循环覆盖及禁止覆盖、64 位时间戳、重开持久化、超时和首错传播，
以及真实 SFUD 和字节级 SPI 模型的完整链路。硬件数据库读写和掉电中断
恢复仍需在板上验证。应用用法见 [app/database](../../app/database/README.md)。

`test_index.c` 还覆盖直接映射与二分共存、稀疏清单、5000 次轮流更新及 GC、
稳定键跨表重排、删除重开、长度变化、CRC 损坏及各写入步骤的失败注入。
当前 4 字节测试值的正常替换为 5 次介质读回调，查询已知不存在项为零次；
这些是主机 NOR 模型的调用计数，不是板上耗时或完整掉电可靠性证明。
