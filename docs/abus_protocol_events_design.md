# aBus 发布订阅与协议调度设计

状态：**首版已实现**。更新日期：2026-10-13。

本文记录讨论后落地的边界、API 和验证方法。公共声明以
[aBus_event.h](../func/aBus/include/aBus_event.h)、
[aModbus.h](../func/aModbus/include/aModbus.h)、
[aModbus_client.h](../func/aModbus/include/aModbus_client.h)、
[aModbus_server.h](../func/aModbus/include/aModbus_server.h)、
[aModbus_mapping.h](../func/aModbus/include/aModbus_mapping.h) 为准。
硬件收发尚需在实际主从板上验证；主机测试不能代替线路联调。

## 1. 职责

| 层 | 负责 | 不承担 |
| --- | --- | --- |
| aBus | 数据访问、关联注册、范围匹配、多播接收与完成 | 协议地址、收发、调度任务 |
| aModbus | 协议编码、校验、主从事务、协议映射与数组回调 | aBus 实现、队列、周期、重试计数 |
| aComm | 公共首成员、数组遍历、周期分组、重试、队列、数据绑定与完成通知 | 协议地址解释、编码协议字节、创建任务 |
| app/protocol | 点表、通信配置、aComm 装配、队列及任务生命周期 | 编码报文、逐帧重试实现 |

aModbus 主从接口分别位于 client/server 头，公共传输位于 aModbus.h。
不设置独立 service 层。aModbus 单向依赖 aComm，不引用 aBus 实现。
应用注册具体协议数组与回调；协议实例内嵌端点，数据端点由应用提供。
应用创建并持有 aComm，调用 Process/Wait；通信适配通过非阻塞 submit 回调提交工作。

## 2. 数据标识与运行状态

目标为 `bus + deviceID + sigIndex + paramIndex`。`ABUS_SIG_WHOLE` 表示整组。
`sigKey` 仍用于稳定业务标识/持久化；远端站号不从 deviceID 推导。

- `aBusTarget_t`：仅定位，运行请求使用。
- `aBusTargetConfig_t`：定位加 `subscribe_events`、`publish_events`。
  配置平铺在协议条目首成员 comm 内，不放进共享 SIG 定义。
- Flash 定义可被多个 bus 共享；每个 bus 的 RAM 链表相互独立。

```text
bus
 └─ SIG 状态
     ├─ data → 整组当前值
     ├─ subscribers → 整组节点 → NULL
     └─ params → 每个 Param 的 subscribers → 字段节点 → NULL
```

Param 状态只保存链表头，不复制字段值；字段值仍在整组 data 中。
节点引用 `type / owner / definition / context / notify`：

| 字段 | 含义 |
| --- | --- |
| type | 提供者类别：MODBUS、DATABASE、APPLICATION，ANY 只用于查询 |
| owner | 非 NULL 的稳定实例身份；总线只比较，不解引用 |
| definition | 具体只读条目，例如 `&client_maps[i]`；普通业务可为 NULL |
| context | 运行上下文，可以由多个节点共享 |
| notify | 统一非阻塞入口；仅标记节点可为 NULL |

节点另保存私有 next、bus 和所属链表头指针，防止跨实例重复挂接并支持解除注册，
不重复存储目标下标。同一节点不能同时挂整组和字段，需要两个节点。
同 owner 的不同节点分别接收，aBus 不按 owner 去重。

## 3. 开关、分配与生命周期

`config/aclass_config.cmake` 中 `ABUS_EVENT_ENABLE` 默认 ON。
OFF 时不分配头/参数状态，不插入自动通知路径，事件 API 不提供；
通用目标定义及配置掩码检查仍可用，既有 Get/Set 和轮询继续工作。

启用后，每个 SIG 增加整组头和 params 指针，每个已声明 Param 增加一个头。
头指针空间约 `(2*N + P) * sizeof(void*)`，另计节点与少量实例状态。

- 动态 aBus 在原有实例/状态分配中一次追加 Param 状态，不逐字段 malloc。
- 静态 aBus 除 sigs/capacity 外，提供 params/param_capacity；没有参数可为空。
- 节点由提供者拥有，aBus 借用。动态 Modbus 实例只按实际登记项分配节点；
  静态产品可按最大数量预留，预留数组仍占 RAM。
- 请求跟踪数组由调用者预先提供；分发/完成不申请堆。
- 定位成本为表查找 O(T) + 下标定位；精确关联查询只遍历所选链。
  广播额外遍历相交字段和节点，不宣称整组分发为 O(1)。

静态存储示例：

```c
static aBusHandle_t instance;
static aBusSigState_t sigs[SIG_COUNT];
#if ABUS_EVENT_ENABLE
static aBusParamState_t params[PARAM_COUNT];
#endif

aBusInstanceStructInit(&instance, sigs, SIG_COUNT);
#if ABUS_EVENT_ENABLE
instance.params = params;
instance.param_capacity = PARAM_COUNT;
#endif
status = aBusInitStatic(instance_id, tables, table_count, &instance);
```

注册/解除注册在启动或停止所有相关调用者后进行，首版不支持热插拔。
关闭顺序：停止新请求 → 排空队列并完成请求 → 停止消费者 → 销毁应用调度器 →
销毁计划/协议 → 销毁总线。仍有节点或借用请求时总线返回 BUSY；仍有计划时
协议不能销毁。应用负责先停止引用计划的作业，再销毁计划。初始化失败撤销
已登记节点；应用回收自己持有的调度资源。
数据存储的 `ABUS_RAM_BIND_EXPORT` 与事件登记完全独立。

## 4. 订阅、发布与关联查询

| 掩码 | 行为 |
| --- | --- |
| 0 | 配置项不登记；直接 Subscribe(0) 是参数错误 |
| `ABUS_SUBSCRIBE_MARK_ONLY` | 仅登记，可查询，不调用 notify |
| `ABUS_EVENT_UPDATED` | 本地成功提交数据后通知 |
| `ABUS_EVENT_WRITE_COMPLETE` | 可选的执行结果观察通知 |
| `ABUS_REQUEST_READ` / `ABUS_REQUEST_WRITE` | 接收主动请求 |

仅标记使用最高位，与真实事件互斥，不能发布。未知位、非法组合在产生作用前拒绝。
`publish_events` 只接受额外的 WRITE_COMPLETE；UPDATED 不由协议开关控制。
直接修改静态绑定变量后，可由应用在完成同步后显式 Publish(UPDATED)。

aBus 的真实事件订阅必须有回调；没有订阅者时 Publish/Request 直接丢弃，
返回 OK、matched=0，不遍历未登记的协议，不产生完成回调。
目标本身不存在或参数非法仍返回 NOT_FOUND/INVALID_PARAM。
队列满等真正拒绝仍返回故障，不能和“无人订阅”混同。

- `aBusSubscribe` / `aBusUnsubscribe`：借用/解除一个节点。
- `aBusAssociationGet`：精确定位链表，按 type、owner、匹配序号枚举，包含标记节点。
- `aBusAssociationFind`：要求唯一，多个匹配返回 BUSY，不选第一项。
- `aBusPublish`：发布 UPDATED 或 WRITE_COMPLETE，可按 owner 筛选接收者。
- `aBusRequest`：单次 READ 或 WRITE，默认多播，可筛选 owner。

查询不存在返回 NOT_FOUND；查询结果只是借用描述，不赋予协议写权限。
提供者自行检查权限、定义和上下文。协议核心不能因 type 不同而调用其他模块。

## 5. 整组与字段互通

aBus 将目标转为 SIG 内的字节区间，输入包含 source、binding、overlap：

- 整组操作投递给整组及全部相交字段节点。
- 字段操作投递给整组、该字段及重叠字段节点；无交集的字段不通知。
- 每个匹配节点只调用一次，整组节点不会因多个字段重复投递。
- 整组写给字段节点时，接收者取原请求中交集对应的字节。
- 字段写命中整组映射时，只处理该字段，不读取旧整组后扩大写入范围。
- 跨接收者无原子性、回滚或完整字段覆盖保证。

aModbus 映射支持完整映射、整组到完整字段，以及完整字段到整组映射。
结构体仍要求连续、无隐式填充、每字段占整数个寄存器。
非完整类型字段的交集返回 UNSUPPORTED，不扩大写入。U32/S32 不写半个值。
同一个 Modbus 主站实例中，订阅 WRITE 的远端地址区间重叠会在初始化拒绝，
防止整组和字段映射对同一远端地址下发两遍。跨实例多播仍合法。

## 6. 接收与异步完成

统一入口：

```c
aStatus_t notify(void *context, const aBusNotifyInput_t *input,
    aBusNotifyOutput_t *output);
```

input 包含原目标、事件、三个区间、具体关联节点、definition、可选写数据和
请求凭据 delivery。普通通知的 input 只在回调期间有效，不能存它的地址。
请求的 delivery 由调用方提供稳定存储，可登记到后台队列。

output.accepted 进入回调时为 false；真正标记/入队后置 true。
回调返回 aStatus 表示投递状况，不表示远端执行完成。
通用订阅者一旦接收请求，后续异常不撤销 accepted，调用者不得重投或复用数据。
aCommSubmit 用返回值明确接收结果：OK 为已接收，其他值均为未接收。
队列解锁异常由 aOSMutexUnlock 统一记录，不将已入队请求报告为提交失败。
aComm 绑定根据 submit 返回值填写 output.accepted；aBus 的多播接收统计保持独立。

每个接收者最终调用 `aBusComplete(delivery, status)` 一次。凭据防止重复结算，
完成回调收到原请求和该接收者身份。凭据复用后，旧指针不能再次完成新请求。WRITE_COMPLETE 只是可选观察通知，
不能替代完成结算或用来管理请求内存。

请求持有分发保护引用，接收者可以在分发返回前完成；只有分发结束且所有
已接收者完成回调返回后，`aBusRequestDone` 才为真。此时才能复用请求、
凭据数组和写数据。不同接收者可在不同任务完成，用户回调自行同步结果。
没有强制取消、等待完成接口或历史值队列。

```c
static aBusRequest_t request;
static aBusDelivery_t deliveries[2];
static uint32_t value;

aBusRequestStructInit(&request, deliveries, 2U); /* 只在首次使用前调用 */

/* 再次投递前检查 Done，保持 value 在接收期间不变。 */
aBusRequestConfig_t config;
aBusRequestConfigStructInit(&config);
config.target.deviceID = device_id;
config.target.sigIndex = sig_index;
config.event = ABUS_REQUEST_WRITE;
config.data = &value;
config.size = sizeof(value);
status = aBusRequest(bus, &config, &request, &result);
```

matched = accepted + rejected；一个失败不妨碍其他节点接收。
凭据容量不足的节点拒绝 BUSY，其他节点照常投递。本次匹配中拒绝项也占用
凭据序号，因此容量按可能匹配数规划，而不是按预计成功数规划。
返回错误仍可能 accepted>0，释放条件始终看 Done。

## 7. 非阻塞约束与自动通知

发布、请求投递及其回调不等待互斥锁/队列空间/通信/完成，不 sleep，不分配堆，
不自旋等其他任务，不同步打印阻塞日志。允许无锁原子、有限查找、NO_WAIT
尝试入队和不等待消费者的唤醒。bool/unsigned 原子类型要求始终无锁，编译期检查。

C 回调的实际行为由提供者保证，aBus 无法自动阻止用户写入阻塞代码。
非阻塞不等于零耗时，也不等于支持 ISR。消费者任务可以按协议超时等待 I/O。

Set/SetParam/SetKey/Update 保留数据锁的 timeout。成功提交且成功解锁后，自动
发布一次 UPDATED；写相同值也通知，不比较旧值。Key 路径不重复发布。
范围失败不通知；解锁失败不回滚，但不在可能仍持锁时调用订阅者。
自动投递失败不改变成功提交的返回值，用 `aBusNotificationFailed` 非阻塞取出
并清除失败标记。需要最终处理最新数据的订阅者使用自己的 pending 标记；
普通通知可合并，不保证每次中间值均被处理。

UPDATED 不隐式转换为远端 WRITE，避免采集→更新→下发循环。

## 8. Modbus 完整映射与轮询

`aModbusClientMap_t` 表示一条主站映射，数组构成完整通信表；
ClientConfig 通过 `maps/map_count` 引用它。临时访问、事件请求和采集计划
共用该表；周期保留在表项内，运行时调度由应用装配的 `aComm` 负责。

- comm 平铺 deviceID、sigIndex、paramIndex、订阅/发布掩码，
  同时持有 flags 和 poll_time_ms。
- area/address/quantity/word_order 定义远端布局，quantity 必须显式填写并匹配。
- flags 同时显式指定 READ/WRITE 权限和可选 BREAK_BEFORE 断帧标志。
- `poll_time_ms=0` 不自动采集，但仍可临时访问。
- `1..INT32_MAX` 是整轮起点周期。
- `ACOMM_POLL_ONCE` 为一次性组，其余大值非法。

初始化按周期稳定分组，各组保持清单顺序，只合并同区域连续地址，不跨空洞、
容量或 BREAK_BEFORE。计划保存启用项的有序下标及预生成帧，直接引用原只读点表。
静态计划提供 indices 和 frames 两个数组，容量可按完整表项数预留；动态按实际帧数与启用项数一次申请。
不同周期及一次性项不能合成一帧。首轮立即具备执行资格，超期不积压补跑。

应用将数组、数量、aModbusClientTableOps 与 context 注册给 aComm；
aComm 内部生成周期组和作业，应用不再逐组装配。
手工 Job 的 poll_time_ms=0 仍表示无整轮间隔；禁用表项不生成作业，不能直接
将零配置传成忙轮询。统一使用 ACOMM_POLL_ONCE（UINT32_MAX），
应用直接传递周期值。aComm 识别标记后执行一轮并停止，普通 Trigger 不复活。

通用作业 JobConfig.retry_count 是每步额外尝试次数，默认零，最多 UINT16_MAX。
aCommPlanExecute 返回 status/advance/retryable；调度器按 retry_count
限制尝试次数，按 error_delay_ms 再调度，不在协议内 sleep。
适配器入口忙不计数；已进入传输的 BUSY 不作为免费重试。

| 结果 | 推进策略 |
| --- | --- |
| 成功 | 推进并清零重试计数 |
| RTU 超时、CRC/帧错误 | 尚有次数时重试，否则推进 |
| 实例 BUSY，未开始事务 | 不消耗次数，延后尝试 |
| 合法协议异常、权限/布局错误 | 不重试，推进 |
| 通信成功但本地提交失败 | 不重发通信，推进 |
| EOF、永久故障、失效的 TCP 连接 | 不重试当前帧，推进 |
| 临时写请求 | 执行一次，不自动重试 |

计数每帧独立，成功帧不重跑。周期组下一轮重新开始；一次性组全部帧成功或
耗尽后停止，显式临时访问仍可执行。

## 9. Modbus 订阅与应用队列

主站只登记 READ/WRITE 请求或 MARK_ONLY，不把 UPDATED 自动转换成协议工作。
ClientConfig.submit 由应用提供，接收稳定的 aCommRequest_t；应用消费者分别
调用 execute 与 complete 一次。只注册事件不要求 Plan 或通用调度器；
aModbus 映射内嵌 aCommBinding，并将协议执行回调注册给它。
主站模块不创建调度器，不读取调度器私有字段。
其他模块可直接用 aBus 订阅 UPDATED。每个实际登记映射有一个 aComm 管理的
非阻塞请求槽，占用或调度队列满时拒绝新请求，不覆盖旧写值。
aComm 在消费者中检查权限及整组/字段交集，再调用协议执行回调。
公共首成员指针直接定位原始表项；Modbus 回调取得地址和字序，不再全表查找。
请求结束后由 aComm 可选发布 WRITE_COMPLETE，并通过数据端点完成借用。

后台成功写应答只报告结果，不覆盖本地采集副本，不自动读回。
周期项由下次采集确认；零周期/已完成一次性项需显式 READ 请求。
从站继续地址→aBus，写入成功自然产生一次 UPDATED；可选登记 MARK_ONLY，
或者订阅 UPDATED/WRITE_COMPLETE 并提供非阻塞 notify。拒绝 REQUEST_READ/WRITE，
不主动向主站发送请求，publish_events 必须为零。普通地址读写不强制创建节点。
未登记项可使用同步显式协议接口，aBus 不自动回退到该路径。

应用 FAN 配置共 50 项：TEST_001～030 每 1000 ms 采集，TEST_031～050 不自动轮询，
全部提供显式读取；TEST_001 只读，其余测试项读写。帧超时 500 ms，额外重试 2 次。
`app/task/test/test_task.c` 在主站且事件开启时，每 5 秒按 FAN TEST_003 的 SIG
提交值 101～200 循环写入。任务等待 Done 后再复用数据并打印上次结果；
完成回调只记录状态。`sig set` 仍修改本地数据，不代表远端写命令。

## 10. D1～D14 落地索引

| 决策 | 落地位置 |
| --- | --- |
| D1 事件掩码 | 通信表平铺在 aCommEntry_t；aBus 运行请求用 aBusTarget_t |
| D2 完整通信表 | aModbusClientMap_t、Plan 的周期分组 |
| D3 提供者身份 | SubscriberConfig、AssociationGet/Find |
| D4 无处理者丢弃 | Publish/Request 的 matched=0 |
| D5 自动 UPDATED | aBus.c 的提交完成路径 |
| D6 分开入口、统一回调 | Publish/Request、NotifyInput/Output |
| D7 多播 | 每节点独立接收/完成，可按 owner 筛选 |
| D8 双层链表 | aBusSigState_t / aBusParamState_t |
| D9 整组字段互通 | source/binding/overlap；协议只处理交集 |
| D10 生命周期 | delivery、分发保护、Done、Submit |
| D11 写应答不覆盖副本 | 主站请求工作项的内部执行函数 |
| D12 普通观察与完成分离 | WRITE_COMPLETE 与 aBusComplete |
| D13 仅标记 | MARK_ONLY 最高位和统一检查 |
| D14 一次性/重试 | aCommJobConfig_t.retry_count + ACOMM_POLL_ONCE |

## 11. 验证入口

```sh
python3 tests/bus_events/run.py
python3 tests/bus/run.py
python3 tests/comm/run.py
python3 tests/modbus/run.py
python3 tests/modbus_demo/run.py
python3 tests/modbus/build_matrix.py
python3 tests/architecture/run.py
cmake --build build/Debug -j6
```

覆盖静/动态状态、三种锁、标记节点、范围互通、提前/并发完成、满槽拒绝、
数据锁外通知、非阻塞入队及已入队解锁故障、按 SIG 写入、部分 STRUCT 写入、
不覆盖采集副本、周期分组、一次性成功/耗尽停止、主从与裁剪构建。
事件关闭时，原有数据测试单独验证零事件路径；事件开启由专门测试和应用链路覆盖。
EOF/CRC、整体/部分寄存器、显式长度、静态绑定与启动回滚沿用原回归测试。
