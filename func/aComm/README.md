# aComm 通信管理

aComm 整个模块只依赖 aOS/aLib，不依赖任何协议栈、aBus 或设备驱动。
它管理具体协议数组的遍历、分组、执行时机、数据绑定和通信通道，
通过注册回调调用协议能力。
不创建任务，不解释协议功能码、CRC 或寄存器编码。端点标签只用于关联查询，
不用于协议分派。

通道模式表示收发机制：STREAM 直接使用字节流，GAP 按静默间隔收集完整帧。
RTU 协议可以使用 GAP，但 GAP 本身不知道协议名称，也不内置任何 RTU 常量。
STREAM/GAP 不决定报文编码；具体协议模块另行选择自身格式。
例如 Modbus 的 RTU/TCP 选择只由 aModbus 解释，不放进 aComm 的模式枚举。

| 公共头 | 职责 |
| --- | --- |
| `include/aComm.h` | 协议注册、通信表、调度及数据绑定接口 |
| `include/aComm_endpoint.h` | 只依赖 aLib 的数据访问、元信息及事件契约 |
| `include/aComm_channel.h` | 流式收发、缓冲、静默分帧和物理发送等待 |
| `include/aComm_instance.h` | 调度、通道、绑定的静态实例布局 |

源文件仍按执行职责分为调度、表计划、通道、端点和绑定五部分。
普通调用方使用前三个头；仅静态资源提供者或嵌入绑定状态的宿主包含 instance。

应用为每个调度实例提供一个执行任务，循环调用 `aCommProcess`、`aCommWait`。
同步回调等待期间，同一实例的其他工作也会等待，独立链路可使用独立任务。
通用调度通过回调执行工作，不按协议类型分支。

## 统一注册协议生命周期

主从都使用 `aCommConfig_t.protocol`，不在 aComm 中增加协议或角色枚举：

```c
const aCommConfig_t config = {
    .protocol = {
        .config = &product_config,
        .items = entries,
        .count = sizeof(entries) / sizeof(entries[0]),
        .ops = &protocol_ops,
        .context = &protocol_context
    },
    .retry_count = 2U,
    .interval_ms = 1U,
    .error_delay_ms = 5U,
    .request_capacity = 4U
};
```

`config` 和 `items` 是不透明指针，仅注册回调解释具体类型。
`protocol_ops` 回调表由应用定义，选择协议模块提供的函数或应用包装函数，
并保持有效至销毁成功。回调的职责：

- `check`：可选，只检查，不分配资源、不开启端口。
- `init`：创建协议实例及映射；成功时可返回通信表，失败须回收本次资源。
- `process`：没有通信表时处理一个服务步骤，由 aComm 自动创建连续作业。
- `deinit`：释放协议实例及映射；先解除采集计划引用，再调用此回调。

有通信表时通过表的 `execute` 回调执行批次，不再调用服务 `process`。
主站采集、从站接收是这两种机制的使用方式，aComm 不据此识别协议。
服务步骤同样受 interval_ms、error_delay_ms 和有限重试规则约束。
只有回调明确返回 retryable 才会重试，协议写操作不会默认重发。

应用负责通道、端口和任务；初始化前停止生产者，注册成功后再启用接收和任务。
初始化回调成功后若调度资源分配失败，aComm 调用 deinit 回收协议资源。
释放回调失败时保留实例供继续释放，并拒绝新的 Process/Wait/投递。
调用者须停止所有使用者并排空请求后销毁；回收失败仍需保留回调 context。
配置描述按值复制；配置对象、数组和 context 借用至释放成功。
回调不得保存 init 入参或输出结构体本身的地址。

protocol 注册、直接 table、手工 jobs 三者互斥；三者均可搭配事件和临时队列。
已有外部协议实例也可直接配置 table/jobs，其所有权仍由外部管理。

## 通信数组与计划回调

协议模块依赖 aComm，协议表项把 `aCommEntry_t` 放在首成员：

```c
typedef struct {
    aCommEntry_t comm;
    /* 后面由协议定义自己的寻址、编码等参数。 */
} MyProtocolEntry_t;
```

公共字段全部由 aComm 定义，目标信息直接平铺，不再嵌套 target：

| 字段 | 含义 |
| --- | --- |
| poll_time_ms | 0 不自动采集；ACOMM_POLL_ONCE 一轮；其余为整轮最小周期 |
| flags | READ/WRITE 读写权限；BREAK_BEFORE 要求从本项开始新批次 |
| deviceID、sigIndex、paramIndex | 数据目标；整组使用 ACOMM_DATA_WHOLE |
| subscribe_events、publish_events | 数据事件的订阅与发布策略 |

具体协议数组保持自己的真实元素类型。aComm 通过 ops.get(table, index)
取得首成员；只有对应的协议回调才能把首成员指针还原为具体表项。
禁止把整个协议数组当成 aCommEntry_t 数组索引，因为两者元素步长不同。

```c
const aCommConfig_t config = {
    .table = {
        .items = entries,
        .count = sizeof(entries) / sizeof(entries[0]),
        .ops = &my_protocol_ops,
        .context = protocol_context
    },
    .retry_count = 2U,
    .interval_ms = 1U,
    .error_delay_ms = 5U,
    .request_capacity = 4U
};
```

应用调用 aCommCreate/InitStatic 后，aComm 自动生成计划和周期作业；
应用不需要创建 Plan、查询分组或把每组转成 Job。table 与手工 jobs 二选一，
两种模式都可同时注册事件和临时队列。

回调均在初始化调用者或 Process 消费任务中运行：

- get：按具体结构体步长定位，必填。
- check：可选，初始化校验具体协议参数。
- merge：可选，判断首项到前项的批次能否继续接入下一项；无回调逐项执行。
- prepare：可选，只生成固定大小的预编码字节缓存，不做 I/O 或申请资源。
- execute：必填，执行选定批次，填写 status/advance/retryable。
- acquire/release：可选且成对，建立与释放协议上下文的借用关系。

aComm 按周期稳定分组，同组保持原表顺序，断组标志优先于 merge。
协议自行判断地址是否连续、报文容量和编码规则，aComm 不保存站号、寄存器
区域、地址、数量或字序。batch 只包含原数组、原表下标及数量。
prepare 字节使用 memcpy 存取，不强转为私有结构体。

数组、回调表和上下文在使用期间必须有效且不变。配置可放 Flash；RAM 仅保存
启用项下标、合并后的批次缓存、周期组状态及队列，不复制完整协议数组。
初始化按实际数量统计容量，运行期不再查表组装调度作业、不申请内存。
独立计划 API 仍可用于测试或其他执行器，统一注册时由 init 回调填写 aCommPlanConfig_t。

## 三种工作

| 类型 | 配置/投递 | 语义 |
| --- | --- | --- |
| 周期作业 | 注册 `table` 自动生成，或提供 `jobs[]` | 一轮包含固定步数，每次执行一步 |
| 事件 | 初始化 `events[]`，`aCommEventPost` | 同一事件重复投递合并，无附带数据 |
| 临时请求 | `aCommSubmit` | 固定容量 FIFO，每条执行一次，随后完成通知 |

三类工作轮转；作业、事件内部各有轮转游标。持续投递事件或请求不会饿死
已到期作业，但延迟仍取决于回调耗时。没有优先级抢占、自动线程或状态机框架。
运行期不分配内存，扫描成本为 O(作业数 + 事件数)，事件投递为 O(1)。

`aCommProcess` 的返回值描述调度器是否正常处理，业务结果在
`result.execution.status`，本次类型在 `result.type`。每次最多调用一个执行
回调；临时请求的完成回调紧随其后。无工作时类型为 `ACOMM_WORK_NONE`。
业务失败不会作为 `Process` 自身错误返回。

## 作业与时间

- `step_count`：一轮的步骤数，例如六帧 Modbus 请求对应六步。
- `poll_time_ms`：整轮实际起点的最小间隔，零表示连续执行各轮；
  `ACOMM_POLL_ONCE` 表示只执行完整一轮。
- `retry_count`：每一步失败后的额外尝试次数，默认零。
- `interval_ms`：成功且推进后的等待，仅约束本作业。
- `error_delay_ms`：失败或未推进后的等待，必须非零。

执行回调填写 `status`、`advance` 和 `retryable`：

- advance=false：尚未执行，例如实例入口忙，保留当前步骤且不消耗重试次数。
- advance=true 且失败可重试：在 retry_count 内延后再执行同一步。
- 成功、不可重试失败或达到上限：推进下一步，清零该步骤的重试计数。

默认结果为 ERROR、不推进；等待由 error_delay_ms 限制，调度器不在内部 sleep。
协议映射实例只判断错误能否重试，不保存计数；写请求不自动重试。

第一轮立即开始。超期不积压历史轮次，也不为赶周期跳过步骤。
`aCommTrigger` 触发全部周期组，`aCommJobTrigger` 触发指定组；两者合并提前执行请求。正在执行的一轮保持进度，随后额外开始一轮。
它绕过整轮周期等待，但仍遵守成功/失败后的最小等待。
单轮作业完成后不重新启动，Trigger 仍返回 OK。
帧间静默由通道维护；事务回调将剩余预算传给通道。两者不同于作业周期。

除 `poll_time_ms` 可使用 `ACOMM_POLL_ONCE`（UINT32_MAX）外，所有配置时间
不超过 INT32_MAX 毫秒。单轮标记不参与时间计算；普通时间使用 aOS 单调
毫秒时基的无符号差值处理回绕。两次处理间隔须小于 uint32_t 毫秒的完整回绕周期。

## 事件通知

```c
static void refreshed(void *context)
{
    /* 在消费任务内检查业务状态；共享数据由应用保证同步。 */
    (void)context;
}

static const aCommEventConfig_t events[] = {
    {.function = refreshed, .context = NULL}
};
```

初始化配置引用 `events` 并设置 `event_count`，数组下标就是事件标识。
其他任务调用 `aCommEventPost(handle, 0U)`；先原子置位待处理标志，再调用
`aOSWaitObjectNotify`。执行任务取出并清除标志后才调用回调，因此回调期间
再次投递会留下下一次待处理。事件数量不受单个 32 位位图限制。

等待对象只负责唤醒，不携带编号或业务数据。先通知后等待也保留通知。
`aCommWait` 重新计算最近期限，避免调用者在 Process 后耗时造成额外超期。
唤醒后必须再次 Process；旧通知可能导致一次立即返回，这是正常行为。
当前 Post/Submit/Trigger 仅用于任务上下文，没有 ISR 投递入口。

事件合并适合“重新检查/刷新”。要求每次执行且携带不同参数的工作应使用
临时请求队列。事件通知不保证 context 中业务对象的快照一致性。

## 临时请求与同步

`aCommSubmit` 复制请求描述，不复制 context 或其引用的缓冲区；引用必须保持
有效至 complete 返回。execute 的签名为 `aStatus_t execute(void *context)`，
只执行一次；complete 接收 context 和状态值，可为空。它不携带步骤编号或
推进标志，失败不自动重试。映射实例的 aBus 请求也使用同一种工作描述。

仅队列容量非零时创建一个队列锁，入队采用不等待锁方式，满或竞争返回 BUSY。
取出后释放锁再执行 execute/complete，回调可以投递新工作。
返回 OK 表示已入队，其他返回值表示未接收；执行结果由完成回调报告。
完成回调可能在 Submit 返回前执行，提交前就要准备好上下文。
直接调用 aOSMutexUnlock，解锁异常由 aOS 统一记录 AOS_FAULT_MUTEX_UNLOCK，
Debug/Release 均保留诊断。已入队请求仍返回 OK，不回滚、不重投，也不能提前
复用引用数据。
故障记录不负责恢复损坏的锁；实际解锁失败可能阻止后续消费，需排查底层故障。
周期状态仅消费者修改；事件/主动触发标志采用原子操作，无每项互斥锁。
同一实例 Process/Wait 重入返回 BUSY，多个实例状态相互独立。

## 初始化与资源

`aCommConfigStructInit` 默认空配置、无队列、表错误等待 5 ms；
Job 初始化默认一步、错误等待 5 ms。
配置结构体被复制，但 table/items、jobs/events 数组及上下文借用至销毁，
不得运行中修改配置。

- `aCommCreate`：一次申请实例和精确大小的状态/队列数组，再创建 OS 对象。
- `aCommInitStatic`：提供零初始化的实例，设置 jobs/events/requests 指针和容量。
  表模式另外提供 plan.frames、plan.indices、table_jobs 及容量；
  类型及布局见 `aComm_instance.h`，容量可按表项数给上限。
  OS 等待对象和锁仍可能动态分配。
- 运行中禁止直接修改或移动实例、状态和队列。
- 销毁前停止所有生产者、执行者和等待者。正在执行或队列未排空返回 BUSY。
  无在途调用时销毁可丢弃合并事件；不会丢弃已接受的临时请求。

等待对象始终创建；没有临时队列时不创建队列锁。
模块开关为 ACOMM_ENABLE、ACOMM_STATIC_ENABLE、ACOMM_DYNAMIC_ENABLE。

```c
for (;;) {
    aCommProcessResult_t result;
    aStatus_t status = aCommProcess(comm, &result);
    if (status != A_STATUS_OK) {
        aOSDelayMs(5U);
        continue;
    }
    /* 实际应用应处理 Wait 的非超时故障，避免连续错误造成忙循环。 */
    (void)aCommWait(comm, result.wait);
}
```

主机验证：`python3 tests/comm/run.py`。

## 一次性作业

Job 配置 `poll_time_ms = ACOMM_POLL_ONCE` 后，在全部步骤推进完成时停止，
JobTrigger 不会重新启动已完成作业。单轮完成时保留
`step_index == step_count` 表示停止；普通周期完成时将下标归零，等待下一轮。
失败时回调设置 retryable=true，通用调度器按 retry_count 限制重试次数，
按 error_delay_ms 再执行同一步；aComm 通用调度不解析协议错误。
普通 poll_time_ms=0 仍表示无整轮间隔，不能把它当作禁用开关。

## 通道与协议注册

`aCommChannelConfig_t` 包含底层 stream、IO 回调、时间参数和可选 frame_check。
初始化复制配置，但上下文和底层资源保持借用。通道不拥有串口或网络连接。

| 模式 | 输入方式 | 帧边界 | 返回约定 |
| --- | --- | --- | --- |
| STREAM | 底层 `stream.read` | 由协议解析累计字节 | 保留部分读写和 EOF，不把一次 read 当完整报文 |
| GAP | `aCommChannelReceive/ReceiveAt` | 配置的静默时间 | read 不跨帧；帧耗尽为 EIO，无帧为 EAGAIN/ETIMEDOUT |

GAP 的 `character_us`、`byte_gap_us`、`frame_gap_us` 由应用提供最终微秒值。
通道不推导或覆盖协议默认值；`byte_gap_us=0` 禁用帧内间隔检查，
`frame_gap_us` 必须大于零，且不小于 `byte_gap_us`。时间戳在字符完成时采集，
接收间隔判断会补上 `character_us`；流式模式忽略这些配置。

初始化根据 `io.ticks_per_second` 使用 64 位乘法、向上取整转换为计数，
拒绝帧间隔与字符时间之和超过 32 位计数半周期的配置。
时基固定频率、32 位自然回绕；运行期逐字节接收只比较计数差。
硬件时基频率改变后须重新创建通道，不能直接修改配置。
完整帧检查回调在消费任务、临界区外运行：OK 接受，NOT_FOUND 忽略并继续等待，
其他错误结束本次读取。过滤不重置超时预算，不在 ISR 做协议解析。

GAP 单生产者允许 ISR 交付字节；线程生产者须使用与 io.enter/exit 相同的保护。
ReceiveAt 使用真实捕获时间，批量补送不能与消费者判断静默交错。
错误、帧内超时和容量耗尽丢弃整帧；满队列丢弃新帧，保留已接收的帧。
默认帧上限 256 字节、队列 3 帧，可通过全工程一致的
ACOMM_CHANNEL_FRAME_SIZE/ACOMM_CHANNEL_FRAME_COUNT 编译定义调整。
静态和动态通道均内置这组固定存储；STREAM 不复制数据，但当前实例布局仍预留
帧存储。后续有大量网络通道时，可以独立裁剪这部分 RAM。

`aCommChannelBindTransport` 返回通用流和 discard_input、prepare_frame、
wait_transmit_complete、finish 回调。同步协议栈通过这些回调读写通道。
prepare_frame 确认上次发送完成并等待静默；finish 只释放当前帧。
STREAM 不自行清空接收数据；未提供的辅助能力以 NULL 表示。
STREAM 的 prepare_frame/finish 始终为 NULL，discard_input 和发送完成回调
只在底层提供相应实现时注册，协议栈不得调用空回调。
flush 只提交输出缓冲，物理发送完成由单独回调确认。

注册分为两部分，不额外维护协议注册表：

1. 把具体协议数组、数量、回调表与 context 填入 table；
   被动服务也可注册普通 Job，临时操作作为 aCommRequest_t 投递。
2. 创建通道，将其传输回调提供给协议栈；需要完整帧检查时，将协议函数填入
   `aCommChannelConfig_t.frame_check/frame_context`。

主从都能注册执行回调。从站回调同步处理一笔请求，不需要采集计划；本应用
用一个连续作业执行它。回调可能阻塞到请求超时，同一实例无法在此期间执行
其他工作，因此独立物理链路由独立消费者推进。本版没有异步编解码状态机。

数据访问通过下述公共端点接入，具体协议与数据容器各自提供实现。
新增协议只实现数组回调，不需要修改通用调度或通道。

关闭时先停止新请求，处理完已接受请求，再停止消费任务和接收生产者，
最后释放调度器、协议、端口和通道；
所有协议传输引用及 ISR 接收引用结束后才能释放通道。通道本身不增加任务锁，
完整事务由单消费者串行执行。静态/动态 API 由 ACOMM_*_ENABLE 控制。


## 数据端点

aCommEndpoint_t 包含只读操作表，具体实现可将它放在自身首成员。
read/write 的 target 为不透明指针，由提供者解释；data 操作组提供数据元信息、
整组更新及可选的订阅、发布、完成通知。extension 仅供提供者保存自己的规则，
aComm 不转换它、不按协议类型分派。

aComm 不再提供带 unit_id、area、address、quantity、word_order 的公共映射。
Modbus 的寄存器映射和编解码在协议模块；通用层不会假定所有协议都使用线性地址。
数据端点可由 aBus 或其他数据容器实现，公共类型使用 aLib 的数据布局定义。

同一 SIG 的快照一致性由数据端点的 update 契约保证。协议扩展负责将其报文
表示转换成业务数据；数据访问和快照流程由 aCommBinding 提供。

## 通用数据绑定

协议运行实例可嵌入 `aCommBinding_t`。配置使用 `aCommBindingConfig_t`，
只包含数据端点、自定义读写回调及可选工作区，没有协议地址、站号或字序。
绑定实现不自行申请堆、不创建锁或任务，由宿主提供生命周期和串行访问保证。

- `aCommBindingRead/Write`：优先调用自定义 SIG 回调，未配置时访问数据端点。
- `aCommBindingTransfer`：读取整个 SIG 的快照，或在数据端点的 update
  同步区间执行转换并提交；转换失败不提交。跨 SIG 不提供事务。
- `aCommBindingSubscribe`：按公共表项的掩码登记节点；未订阅项不占槽位。
- `aCommBindingDeInit`：先检查在途请求，再解除节点；仍被借用时返回 BUSY。

事件通过 `aCommBindingEvents_t` 注册回调。REQUEST 通知只占用槽位并调用
非阻塞 submit；消费者执行时，aComm 校验目标、权限和整组/字段重叠区间，
把实际目标及写请求数据传给 execute。execute 再解释具体协议表项。
普通 UPDATED/WRITE_COMPLETE 在通知现场调用 observe，必须保持非阻塞。
MARK_ONLY 只登记关系，不执行工作。观察通知不会自动转换为写请求。

每个已接收请求完成一次；aComm 按 publish_events 可选发布 WRITE_COMPLETE，
再释放槽位并调用数据端点 complete。完成回调可以重新提交请求。
请求数据借用到完成回调结束；队列拒绝不会保留槽位，已接受请求不会撤销接收。
协议模块无需重复实现 busy 标记、请求排队和完成通知。

静态宿主提供 `aCommBindingSubscription_t[]`，并为每个槽填入数据端点原生节点。
动态宿主通过 StorageSize/StorageSetup 将槽位和原生节点放入宿主的一次分配中；
这两个接口只计算和布置内存，不调用 Alloc。绑定的分配方式跟随宿主，
不要求创建 aComm 调度器，可注册应用自己的执行队列。
默认工作区为 ACOMM_VALUE_CAPACITY；大 SIG 可配置独占外部工作区。

独立测试 `python3 tests/comm/run.py` 的绑定用例不链接协议、aBus 或 OS 实现，
覆盖请求拒绝、同步完成、重入投递、字段范围、快照提交和存储对齐。

应用实例见 [app/protocol](../../app/protocol/README.md)，具体协议数组例子见
[aModbus](../aModbus/README.md)。aComm 的头文件、CMake 与目标文件都不引用
具体协议或 aBus。
