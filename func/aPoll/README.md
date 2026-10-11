# aPoll 调度与事件

本模块只依赖 aOS/aLib，管理周期作业、可合并事件和临时请求，不创建任务。
应用为每个实例提供一个执行任务，循环调用 `aPollProcess`、`aPollWait`。
回调在这个任务中执行；同步回调等待期间，同一实例的其他工作也会等待。
独立物理链路可使用独立实例及任务。模块不依赖 aModbus/aBus 或板级设备。

## 三种工作

| 类型 | 配置/投递 | 语义 |
| --- | --- | --- |
| 周期作业 | 初始化 `jobs[]` | 一轮包含固定步数，每次执行一步 |
| 事件 | 初始化 `events[]`，`aPollEventPost` | 同一事件重复投递合并，无附带数据 |
| 临时请求 | `aPollSubmit` | 固定容量 FIFO，每条执行一次，随后完成通知 |

三类工作轮转；作业、事件内部各有轮转游标。持续投递事件或请求不会饿死
已到期作业，但延迟仍取决于回调耗时。没有优先级抢占、自动线程或状态机框架。
运行期不分配内存，扫描成本为 O(作业数 + 事件数)，事件投递为 O(1)。

`aPollProcess` 的返回值描述调度器是否正常处理，业务结果在
`result.execution.status`，本次类型在 `result.type`。每次最多调用一个执行
回调；临时请求的完成回调紧随其后。无工作时类型为 `APOLL_WORK_NONE`。
业务失败不会作为 `Process` 自身错误返回。

## 作业与时间

- `step_count`：一轮的步骤数，例如六帧 Modbus 请求对应六步。
- `poll_time_ms`：整轮实际起点的最小间隔，零表示连续执行各轮。
- `interval_ms`：成功且推进后的等待，仅约束本作业。
- `error_delay_ms`：失败或未推进后的等待，必须非零。

执行回调填写 `status` 与 `advance`。尚未开始就资源忙时可不推进；已尝试
通信但超时时通常推进，下一轮再尝试。默认结果为 ERROR、不推进，遗漏赋值
不会产生零等待的忙循环。

第一轮立即开始。超期不积压历史轮次，也不为赶周期跳过步骤。
`aPollJobTrigger` 合并提前执行请求；正在执行的一轮保持进度，随后额外开始一轮。
它绕过整轮周期等待，但仍遵守成功/失败后的最小等待。
协议的帧间静默和事务超时由执行回调所在模块维护，不属于这些调度时间。

所有配置时间不超过 INT32_MAX 毫秒，使用 aOS 单调毫秒时基的无符号差值处理
回绕。两次处理间隔须小于 uint32_t 毫秒的完整回绕周期。

## 事件通知

```c
static void refreshed(void *context)
{
    /* 在消费任务内检查业务状态；共享数据由应用保证同步。 */
    (void)context;
}

static const aPollEventConfig_t events[] = {
    {.function = refreshed, .context = NULL}
};
```

初始化配置引用 `events` 并设置 `event_count`，数组下标就是事件标识。
其他任务调用 `aPollEventPost(handle, 0U)`；先原子置位待处理标志，再调用
`aOSWaitObjectNotify`。执行任务取出并清除标志后才调用回调，因此回调期间
再次投递会留下下一次待处理。事件数量不受单个 32 位位图限制。

等待对象只负责唤醒，不携带编号或业务数据。先通知后等待也保留通知。
`aPollWait` 重新计算最近期限，避免调用者在 Process 后耗时造成额外超期。
唤醒后必须再次 Process；旧通知可能导致一次立即返回，这是正常行为。
当前 Post/Submit/Trigger 仅用于任务上下文，没有 ISR 投递入口。

事件合并适合“重新检查/刷新”。要求每次执行且携带不同参数的工作应使用
临时请求队列。事件通知不保证 context 中业务对象的快照一致性。

## 临时请求与同步

`aPollSubmit` 复制请求描述，不复制 context 或其引用的缓冲区；引用必须保持
有效至 complete 返回。execute 的 step_index 固定为零，只执行一次，失败不
自动重试。complete 可为空；非空时成功、失败都会调用，结果仅在回调期间有效。

仅队列容量非零时创建一个队列锁，入队采用不等待锁方式，满或竞争返回 BUSY。
取出后释放锁再执行 execute/complete，回调可以投递新工作。
若底层解锁异常，已接收的请求不回滚；不能盲目重投。正常 aOS 使用下解锁应成功。
周期状态仅消费者修改；事件/主动触发标志采用原子操作，无每项互斥锁。
同一实例 Process/Wait 重入返回 BUSY，多个实例状态相互独立。

## 初始化与资源

`aPollConfigStructInit` 默认空配置、无队列；Job 初始化默认一步、错误等待 5 ms。
配置结构体被复制，但 jobs/events 数组及上下文借用至销毁，不得运行中修改配置。

- `aPollCreate`：一次申请实例和精确大小的状态/队列数组，再创建 OS 对象。
- `aPollInitStatic`：提供零初始化的实例，设置 jobs/events/requests 指针和容量。
  类型及布局见 `aPoll_instance.h`；OS 等待对象和锁仍可能动态分配。
- 运行中禁止直接修改或移动实例、状态和队列。
- 销毁前停止所有生产者、执行者和等待者。正在执行或队列未排空返回 BUSY。
  无在途调用时销毁可丢弃合并事件；不会丢弃已接受的临时请求。

等待对象始终创建；没有临时队列时不创建队列锁。
模块开关为 APOLL_ENABLE、APOLL_STATIC_ENABLE、APOLL_DYNAMIC_ENABLE。

```c
for (;;) {
    aPollProcessResult_t result;
    aStatus_t status = aPollProcess(poll, &result);
    if (status != A_STATUS_OK) {
        aOSDelayMs(5U);
        continue;
    }
    /* 实际应用应处理 Wait 的非超时故障，避免连续错误造成忙循环。 */
    (void)aPollWait(poll, result.wait);
}
```

主机验证：`python3 tests/poll/run.py`。
