# aOS 目录边界

`include/aOS.h` 是本项目的 OS 无关公共接口。`aOS.c` 是当前 FreeRTOS 后端，
负责把任务、mutex、递归 mutex、等待对象、单调时基、errno 和故障记录映射到
FreeRTOS API。

根目录中的 `tasks.c`、`queue.c`、`list.c`、`timers.c`、`event_groups.c`、
`stream_buffer.c`、`croutine.c`、`include/*.h`（FreeRTOS 公共头文件）以及
`portable/` 均来自 FreeRTOS 上游。它们按 `CMakeLists.txt` 中的
`FREERTOS_SOURCES` 和 `FREERTOS_PORT_SOURCES` 单独选入 target；工程自有适配仅由
`aOS.c` 实现。上游许可随内核文件保留，项目不在这些源文件中加入平台业务逻辑。

当前只集成 FreeRTOS。后续增加其他 OS 时新增独立 backend 并保留 `aOS.h` 契约；
device 和 func 不应包含 FreeRTOS 头文件。
