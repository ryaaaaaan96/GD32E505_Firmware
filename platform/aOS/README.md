# aOS 目录边界

`public/aOS.h` 是唯一对上层公开的 OS 接口。`backend/freertos/aOS_freertos.c` 是当前 FreeRTOS 后端，
负责把任务、mutex、递归 mutex、等待对象、单调时基、errno 和故障记录映射到
FreeRTOS API。

目录按公共契约、后端适配、上游内核区分：

```text
aOS/
├── public/aOS.h                对上层唯一公开接口
├── backend/freertos/
│   ├── aOS_freertos.c          自有 FreeRTOS 适配实现
│   └── kernel/                 上游内核源码、include、portable、LICENSE
├── config/                    可复用 CMake 默认值
├── templates/                 生成配置头的模板
├── CMakeLists.txt
└── README.md
```

`backend/freertos/kernel/` 中的 `tasks.c`、`queue.c`、`list.c`、`timers.c`、
`event_groups.c`、`stream_buffer.c`、`croutine.c`、`include/` 以及
`portable/` 均来自 FreeRTOS 上游。它们按 `CMakeLists.txt` 中的
`FREERTOS_SOURCES` 和 `FREERTOS_PORT_SOURCES` 编入私有的 aOS_freertos_kernel 对象目标，
使用公共构建选项，不继承自有代码的严格告警策略。FreeRTOS 头文件、port 和生成配置均不对上层暴露。
上游许可随内核文件保留。

当前只集成 FreeRTOS。后续增加其他 OS 时新增独立 backend 并保留 `aOS.h` 契约；
device 和 func 不应包含 FreeRTOS 头文件。

## 可选系统 workqueue

参照 [Zephyr workqueue](https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html)
的普通工作项语义：专用线程、FIFO、初始化固定处理函数、重复提交合并、执行中可再次排队、
取消区分排队与运行、每项后 yield。它不是 USART 回调的必经路径。
当前实现系统普通队列；尚未提供 Zephyr 的自定义多队列、delayable/triggered work、
queue drain/plug 或全部 k_work API，不宣称完整兼容 Zephyr。

配置 AOS_WORKQUEUE_ENABLE 经集中解析生成 AOS_WORKQUEUE_ENABLE。
默认 OFF；关闭时不创建线程、不分配栈/TCB，工作项操作声明与实现一同裁剪。
通知、等待对象和定时服务独立存在；USART 不依赖 workqueue。
开启时 AOS_WORKER_STACK_BYTES/PRIORITY 决定系统工作线程资源。
aOSInit 首次在调度器启动前串行调用，重复调用不重建或清除诊断。
业务任务仍由 app 创建，worker 不复用为应用启动任务。

```c
aOSWorkItemInit(&work, handler, argument);
aOSWorkSubmit(&work); /* 自动选择任务/ISR；不改变已经排队项的位置 */
```

处理函数在线程上下文执行；系统队列要求短小、不阻塞，耗时业务交给专用应用任务。
通知为队列唤醒，不是事件存储；一项多次提交可以合并，逐次数据必须由 app 自己保存。
工作项和参数在忙碌期间不能修改或释放。取消正在执行的项不打断函数，
而是拒绝重新提交，直到函数退出；取消排队项从链表移除。
aOSWorkCancelSync/aOSWorkWaitIdle 是生命周期屏障，调用前须停止其他生产者，
成功后才可回收；ISR/worker 不能同步等待自身。当前等待用 1 ms 延时检查，
不是 Zephyr k_work_flush 的精确快照语义。

队列入队/出队 O(1)，取消扫描为 O(n)，都使用短临界区；处理函数在区外。
仅在 worker 将要睡眠时通知一次，避免重复唤醒；每项执行后 yield，
但 FreeRTOS yield 不保证低优先级任务得到运行机会，不能替代合理的优先级和耗时预算。

主机回归：SANITIZE=1 python3 tests/aos/run.py。
覆盖初始化、合并通知、重入队、取消与睡眠边界；不能替代硬件延迟和栈高水位验证。

## 跨上下文通知与定时器生命周期

`aOSNotifyGive(wait_object)` 自动选择任务/ISR 通知后端，可用于来源不固定的设备
事件回调；上层不需要判断 CPU 异常号。它只锁存条件变化，不保存事件记录。
ISR 仍必须满足 FreeRTOS 可调用系统服务的优先级要求。

定时器在 Create 时分配包装对象和删除确认信号量；Start/Stop 不分配内存。
Start 的成功表示命令入队，不代表服务线程已处理；失败保留先前有效计时。
Stop 立即禁用尚未进入的回调，即使停止命令队列满也不会再调用它，但已经进入的
回调可以继续执行。Start 对旧的提前到期事件进行过滤；其他任务不能在旧回调
尚未退出时重新启动同一定时器，自身定时回调允许重新计时。

Destroy 返回状态：只有成功才将句柄置空并释放包装对象。实现先禁用回调，提交
DELETE，再提交定时服务 FIFO 确认并等待；确认之后回调不再访问包装对象或 argument。
命令失败保留对象供重试，禁止忽略失败后释放 callback argument。
调度器未运行或 ISR 调用返回 NOT_READY；定时服务自身调用返回 BUSY，避免自锁。
调用者必须串行化生命周期操作，且等待时不能持有回调需要的锁。
若回调投递了其他工作，Destroy 不自动等待这些工作：当前 USART 不再提交 worker 工作，DeInit 在异步操作结束后销毁定时器再释放设备资源。

定时服务屏障依赖 `FREERTOS_INCLUDE_xTimerPendFunctionCall=1`，构建时校验。
主机测试还覆盖停止队列满、旧到期抑制、销毁拒绝/重试和任务/ISR 通知路径。

## 任务创建和故障

任务栈和 AOS_WORKER_STACK_BYTES 均以字节配置；0 栈参数使用后端默认值。
入口允许返回，包装入口自动调用 aOSTaskExit。入口参数包装在启动前保存在保留的
TLS 槽 1，任务启动或被提前删除时释放；TLS 槽 0 仍用于 errno。
分配失败钩子仅记录诊断并返回，应用自行决定是否停止；栈溢出仍停止运行。
当前支持的时基为 32 位 tick、1000 Hz，构建期拒绝其他组合。
有限锁等待即使为 UINT32_MAX 毫秒也会分段到期，不等价于 FOREVER。

任务创建使用 `aOSCreateTask(const aOSTaskConfig_t *config, aOSTaskHandle_t *handle)`。
`aOSTaskConfigStructInit(&config)` 重置全部参数；也可用
`aOSTaskConfig_t config = AOS_TASK_CONFIG_DEFAULT` 初始化。默认名称为 `"task"`，
优先级为 `AOS_TASK_PRIO_NORMAL`，栈字节数为 0（后端默认），argument 和 function
为 NULL；调用前必须填写 function。全零配置不是有效的默认配置。

```c
aOSTaskConfig_t config;
aOSTaskConfigStructInit(&config);
config.name = "service";
config.function = serviceTask;
config.stack_bytes = 2048U;
aStatus_t status = aOSCreateTask(&config, &service_handle);
```

配置结构体仅在创建期间读取；后端复制名称（可能截断），不保存配置指针。
argument 指向的对象须在任务使用期间有效。输出句柄可为 NULL，创建失败时清空输出。
任务可能在创建调用返回前开始运行；默认栈仅提供后端基础容量，业务按实际使用配置。
