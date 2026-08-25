# aMemory（FAL）

本模块与 Demo 保持相同职责：只向 FlashDB 提供硬件无关的 FAL 设备和分区接口，
不负责初始化 Flash25Q，也不持有板级引脚配置。

当前分区：

- `param`：偏移 `0x00100000`，大小 `128 KiB`
- `log`：偏移 `0x00120000`，大小 `512 KiB`

应用层应先调用 `aDevFlash25qInit()` 完成设备初始化。aMemory 随后通过设备索引 0
取得已初始化的 Flash25Q 句柄，并向 FlashDB 提供读、写和擦除操作。
