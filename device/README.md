# Device 层接口规则

公共接口遵循 [接口设计规范](../docs/interface_contract.md)。

统一同类设备跨平台的输入输出与行为约定，不把不同设备统一成同一个函数。
例如 USART 保留 Read/Write，LED 保留 Set/Toggle；参数类型、错误、超时、
生命周期及异步回调上下文遵循公共规则，设备特有语义写入对应头文件。

device 不管理应用产品编号和启动顺序；当前设备映射与显式初始化由
app/devices 管理，详见 [应用设备映射](../docs/architecture.md)。

## Flash25Q 迁移设计

沿用 [aDev_Flash25q](aDev_Flash25q/README.md) 封装官方 SFUD，项目 port
对接 aDrvSpi。公共业务 API 隐藏 SFUD 类型，当前已实现 SPI 后端及
SPI1 板级初始化；QSPI 后端待实现。详见[链路设计](../docs/spi_flash_design.md)。
不新增 a25q 或通用 SPI device 层。
