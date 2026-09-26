# Device 层接口规则

公共接口遵循 [接口设计规范](../docs/interface_contract.md)。

统一同类设备跨平台的输入输出与行为约定，不把不同设备统一成同一个函数。
例如 USART 保留 Read/Write，LED 保留 Set/Toggle；参数类型、错误、超时、
生命周期及异步回调上下文遵循公共规则，设备特有语义写入对应头文件。

device 不管理应用产品编号和启动顺序；当前设备映射与显式初始化由
app/devices 管理，详见 [应用设备映射](../docs/device_registry.md)。
