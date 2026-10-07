# aDataBase

封装官方 FlashDB，提供 KV 参数存储和 TSDB 时序存储。业务头文件
[aDataBase.h](aDataBase.h) 仅使用项目类型，不暴露 FlashDB 或 SPI 类型。
官方源码位于 `FlashDB/`，提交为
`db0afd954ea0f11397e43f6baf3654979d446345`，在此基础上增加 aMemory
存储适配补丁，KV/TSDB 算法源文件保持原样。

```text
应用 KV / TSDB
    → aDataBase → FlashDB → aMemory → 应用设备回调
    → Flash25q → SFUD → SPI 移植层 → aDrvSpi
```

## 文件职责

| 文件或目录 | 职责 |
| --- | --- |
| aDataBase.h / aDataBase.c | 公共请求、生命周期、KV 和 TSDB 操作 |
| aDataBase_instance.h | 静态分配所需的私有实例布局 |
| aDataBase_internal.h | 模块内部的事务和实例管理声明 |
| config/fdb_cfg.h | 项目 FlashDB 配置，开启 KV、TSDB、64 位时间戳 |
| config/fdb_memory_port.h | 向存储补丁提供总预算和首错记录 |
| port/memory_port.c | 模块锁、事务状态及 aMemory 生命周期引用 |
| patches/0001-amemory.patch | 四个上游文件的可重放存储适配补丁 |
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

官方四个文件的修改保存在 patches/0001-amemory.patch。重新下载上述
基线版本后，在项目根目录执行一次：

```sh
git -C func/aDataBase/FlashDB apply ../patches/0001-amemory.patch
```

已打补丁的目录不要重复应用。升级上游版本时需重新检查补丁和测试，
构建脚本会在未找到 aMemory 模式时给出错误，不会自动修改上游目录。

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
