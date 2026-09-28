# 中断与工作队列职责

aDrv 的硬件回调由 aDev 注册，app 不能直接覆盖硬件 IRQ 分发。
串口业务回调仅通过 ReadAsync/WriteAsync 设置，不提供单独 CallbackSet 或 ISR/worker 注册。
回调直接在事件来源执行，遵守非阻塞、有界处理约束。
需要延后执行时由 app 主动使用 aOS 通知或可选 workqueue。

当前 USART 契约与未来 RX session 见 [aDevUsart README](../device/aDev_usart/README.md)；
workqueue 契约见 [aOS README](../platform/aOS/README.md)。
本文件替代此前包含 CallbackSet、强制 RX session 迁移的目标方案。
