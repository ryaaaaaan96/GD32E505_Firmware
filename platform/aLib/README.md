# aLib

不依赖 MCU、RTOS、链接脚本或 C 运行库的纯 C 基础层，提供统一状态、超时与错误
类型、`aStatusToErrno()` 状态映射、编译器属性和纯计算工具。errno 的任务局部
存储仍由 aOS 提供；GCC newlib syscall 不属于 aLib。
