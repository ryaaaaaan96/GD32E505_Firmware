# USART 设计

当前契约统一维护在 [aDevUsart README](../device/aDev_usart/README.md)，
完整函数签名、参数和错误见 [aDev_usart.h](../device/aDev_usart/include/aDev_usart.h)。

本轮收敛：Async 请求是唯一业务回调入口；RX 持续消费 ring，IRQ RX 零拷贝、DMA RX 使用调用者稳定快照区；
所有 Async 回调统一在 ISR 中执行，取消成功表示已受理；
不提供请求链表/token；收发只使用 mode 的方向字段；
USART 不依赖 workqueue。RX session 仅为 README 中的未来扩展。
