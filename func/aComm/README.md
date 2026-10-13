# aComm 通信管理

aComm 整个模块只依赖 aOS/aLib，不依赖任何协议栈、aBus 或设备驱动。
它管理执行时机、通信通道和数据映射，通过应用注册的端点与回调调用协议能力。
不创建任务，不解释协议功能码、CRC 或寄存器编码。端点标签只用于关联查询，
不用于协议分派。

通道模式表示收发机制：STREAM 直接使用字节流，GAP 按静默间隔收集完整帧。
RTU 协议可以使用 GAP，但 GAP 本身不知道协议名称，也不内置任何 RTU 常量。

| 部分 | 职责 |
| --- | --- |
| `include/aComm.h`、`src/aComm.c` | 周期、有限重试、事件、队列、等待通知 |
| `include/aComm_channel.h`、`src/aComm_channel.c` | 流式收发、缓冲、静默分帧和物理发送等待 |
| `aComm_endpoint.h` | 首成员端点、函数表、数据元信息与地址规则契约 |
| `aComm_mapping*.c` | 数据路由、主从映射、采集分组、订阅与完成通知 |
| `include/*_instance.h` | 应用静态分配布局 |

应用为每个调度实例提供一个执行任务，循环调用 `aCommProcess`、`aCommWait`。
同步回调等待期间，同一实例的其他工作也会等待，独立链路可使用独立任务。
通用调度通过回调执行工作，不按协议类型分支。

## 三种工作

| 类型 | 配置/投递 | 语义 |
| --- | --- | --- |
| 周期作业 | 初始化 `jobs[]` | 一轮包含固定步数，每次执行一步 |
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
`aCommJobTrigger` 合并提前执行请求；正在执行的一轮保持进度，随后额外开始一轮。
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

`aCommConfigStructInit` 默认空配置、无队列；Job 初始化默认一步、错误等待 5 ms。
配置结构体被复制，但 jobs/events 数组及上下文借用至销毁，不得运行中修改配置。

- `aCommCreate`：一次申请实例和精确大小的状态/队列数组，再创建 OS 对象。
- `aCommInitStatic`：提供零初始化的实例，设置 jobs/events/requests 指针和容量。
  类型及布局见 `aComm_instance.h`；OS 等待对象和锁仍可能动态分配。
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

1. 将协议执行函数和 context 填入 `aCommJobConfig_t.execute/context`，或作为
   `aCommRequest_t` 投递；aComm 不解读 context 内的协议请求。
2. 创建通道，将其传输回调提供给协议栈；需要完整帧检查时，将协议函数填入
   `aCommChannelConfig_t.frame_check/frame_context`。

主从都能注册执行回调。从站回调同步处理一笔请求，不需要采集计划；本应用
用一个连续作业执行它。回调可能阻塞到请求超时，同一实例无法在此期间执行
其他工作，因此独立物理链路由独立消费者推进。本版没有异步编解码状态机。

映射通过下述公共端点接入，具体协议与数据容器各自提供实现。
新增符合端点能力契约的协议无需修改通用调度、映射或通道。

关闭时先停止新请求，处理完已接受请求，再停止消费任务和接收生产者，
最后释放调度器、协议、端口和通道；
所有协议传输引用及 ISR 接收引用结束后才能释放通道。通道本身不增加任务锁，
完整事务由单消费者串行执行。静态/动态 API 由 ACOMM_*_ENABLE 控制。


## 端点与映射

`aCommEndpoint_t` 只有一个只读函数表指针。具体端点将它放在首成员：

```c
typedef struct {
    aCommEndpoint_t base;
    MyProtocolHandle_t *protocol;
} MyEndpoint_t;
```

回调收到 base 后，只能恢复实际创建的同一种外层对象。禁止把任意协议句柄
强转为端点，或把两个布局相似但类型不同的请求结构体互相强转。
端点由应用持有，不额外分配内存或创建锁，使用期间不得移动、重建或释放。

- `read/write`：同步读写，target 由该端点解释；数据只在调用期间借用。
- `data`：元信息、整组快照更新，以及可选的订阅/发布/完成能力。
- `address`：线性地址区域、容量、类型编码、字段切片、相邻合并和可选预编码。

当前线性映射使用 16 位地址，地址单位可为位或整数字节。
复杂非线性寻址协议仍可通过通用 Job/Request 回调运行，不应硬塞进地址映射。
地址编码后的缓冲区表示由端点定义，通用层不执行寄存器大小端转换。
范围和字段布局共用 aLib 的 `aDataRange_t/aDataField_t`，不依赖 aBus 类型。

`aCommMapping_t` 借用数据端点与协议端点，持有配置副本、独占快照和订阅运行槽。
主站的 client 文件负责主动读写和请求事件；server 文件负责被动地址读写。
两者共用数据路由和生命周期代码；拆分源文件不会额外创建任务或锁。
协议栈仍需保留自己的 client/server：发起事务和应答请求属于协议职责。

从站 maps 与 ranges 平级，各自按 area/address 排序且不得重叠，允许空洞。
查找用二分定位；地址段回调失败不再尝试离散映射。
默认同一 SIG 只取一次快照或在 update 中提交，跨 SIG 不提供回滚。
可选 sig_read/sig_write 由应用负责权限、范围和同步。

内部值缓冲与快照默认 `ACOMM_VALUE_CAPACITY=256` 字节。大 SIG 可另外提供
snapshot/snapshot_size；单个传输目标和合并后的协议数据仍不能超过值缓冲容量。
端点能力表和数据定义在映射使用期间必须保持不变。

## 计划和订阅

`aCommPlanCreate/InitStatic` 按周期稳定分组，调用端点 merge 判断能否合并。
不会跨过 `ACOMM_MAP_FLAG_BREAK_BEFORE` 或工作区容量；没有 merge 则逐项执行。
可选 prepare/read_prepared 成对提供，准备失败使初始化失败，不留下活动计划。
无预编码能力则执行普通读取；预编码字节由端点复制解释，禁止直接类型强转。

映射 poll_time_ms=0 不进入计划，ACOMM_POLL_ONCE 表示只执行一轮。
应用通过 PlanGroupCount/Get 为每组注册 Job，execute 调用 PlanExecute；
周期计时和重试次数仍由调度器维护。注意 Job 的零周期表示连续运行，与映射
清单中的“不自动采集”不同。临时请求只执行一次，不自动重试写入。

主站订阅 REQUEST_READ/REQUEST_WRITE 或仅登记 MARK_ONLY，不把 UPDATED
自动转为远端写入。从站订阅变化作观察用途，不承担主动向远端读写的请求。
通知只占用槽位并尝试 submit；I/O 在消费者执行工作项时进行。
已接受的请求借用数据至完成回调。整组与字段按交集匹配，写入使用请求数据，
不从最新 RAM 重新取值。关闭数据端点的事件能力后不建立订阅节点。

静态映射提供 subscriptions 数组，每个使用槽的 native 指向端点规定的原生
订阅对象；例如 aBusEndpointSubscription_t。只为需要订阅的项准备节点。
动态映射一次分配实例、通用槽和对齐后的原生节点，不逐项分配。
映射、计划、通道、调度器都跟随 ACOMM_STATIC/DYNAMIC_ENABLE。
协议实例的分配方式独立配置，不要求和 aComm 相同。

应用装配示例见 app/protocol/protocol.c：

```text
主站：Job/临时请求 → aComm 映射→ 协议端点 → 协议事务
                              ↔ 数据端点 ↔ 本地数据
从站：协议请求 → 协议端点地址回调 → aComm 映射→ 数据端点
```

aModbusEndpoint_t 提供协议地址编码及事务回调，aBusEndpoint_t 提供数据存取
及事件桥接。aComm 的头文件、CMake 和目标文件不引用这两个具体模块。
不再提供独立的 aModbusAdapter 模块或配置开关。
