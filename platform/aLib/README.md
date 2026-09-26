# aLib

不依赖 MCU、RTOS、链接脚本或 C 运行库的纯 C 基础层，提供统一布尔、状态、
超时与错误类型、`aStatusToErrno()` 状态映射、编译器属性和纯计算工具。errno
的任务局部存储仍由 aOS 提供；GCC newlib syscall 不属于 aLib。

## 布尔类型

项目自研代码统一使用 `aBool_t`、`A_TRUE` 和 `A_FALSE`，不在其他层直接暴露
标准 C `bool`。`aBool_t` 封装标准 C 布尔类型，只表达软件逻辑，不承诺固定存储
尺寸、结构体布局或外部 ABI。

以下场景不得使用 `aBool_t`：

- 外设寄存器、DMA 数据和位掩码；
- 通信协议、文件、Flash 或数据库持久化格式；
- 必须固定宽度或跨编译器共享的二进制接口。

这些边界使用 `uint8_t`、`uint16_t`、`uint32_t` 等固定宽度类型，并通过显式
编码/解码转换为 `aBool_t`。多个可组合状态使用无符号整数位掩码，不使用 C
位域；位掩码作为条件时必须与 `0U` 显式比较。

FlashDB、FreeRTOS、CMSIS 和 GD32 标准外设库等第三方源码保留各自原始布尔
类型。项目封装层负责在第三方 API 边界转换，避免修改上游源码。

## aFifo.h

调用者提供固定容量、固定元素大小的存储；Init/Push/Peek/Pop/Count。
不分配内存，不加锁，调用方必须串行化同一 FIFO 的全部操作。
队满返回 BUSY，队空返回 NOT_READY，失败不改变索引。

元素按值复制。保存指针的元素只复制指针，不复制或释放 payload。
USART 使用 FIFO 保存 TX 请求描述符；超时、取消、回调、DMA 调度均留在 device。
FIFO 不是无锁 SPSC 队列，也不直接向 DMA 出借槽位。

测试：`python3 tests/usart/run.py` 包含 FIFO 空/满、回绕、Peek 和非法容量检查。
