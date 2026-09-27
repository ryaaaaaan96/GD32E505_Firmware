# aOS 目录边界

`public/aOS.h` 是唯一对上层公开的 OS 接口。`backend/freertos/aOS_freertos.c` 是当前 FreeRTOS 后端，
负责把任务、mutex、递归 mutex、等待对象、单调时基、errno 和故障记录映射到
FreeRTOS API。

根目录中的 `tasks.c`、`queue.c`、`list.c`、`timers.c`、`event_groups.c`、
`stream_buffer.c`、`croutine.c`、`include/*.h`（FreeRTOS 公共头文件）以及
`portable/` 均来自 FreeRTOS 上游。它们按 `CMakeLists.txt` 中的
`FREERTOS_SOURCES` 和 `FREERTOS_PORT_SOURCES` 编入私有的 aOS_freertos_kernel 对象目标，
使用公共构建选项，不继承自有代码的严格告警策略。FreeRTOS 头文件、port 和生成配置均不对上层暴露。
上游许可随内核文件保留。

当前只集成 FreeRTOS。后续增加其他 OS 时新增独立 backend 并保留 `aOS.h` 契约；
device 和 func 不应包含 FreeRTOS 头文件。

## 工作任务与应用任务

aOS 的单 worker 是平台延迟执行服务，不是业务任务；func 不得创建或删除任务，
Shell 等业务任务由 app/task 创建并管理。FreeRTOS 自身 idle/timer task 仍由内核管理。

worker 回调串行执行，必须短小且不能阻塞。耗时工作应投递给 app 自己的任务。
config/aclass_config.cmake 的 AOS_WORKER_STACK_WORDS、AOS_WORKER_PRIORITY 控制其资源，
单位为栈字而非字节；构建时检查范围。aOSIsWorkContext 可检查当前是否为 worker，
worker 内等待在途工作退出会返回 BUSY，避免自锁。USART DeInit 和 TX Queue
WaitDrained 同样拒绝该上下文。不能因此认为任意阻塞 API 都能在回调中安全使用。
