# JSON 配置输入

本目录预留给 IDU、FAN 等设备的 JSON 配置，后续用于生成测点清单、
Modbus 寄存器映射和采集配置。当前尚未定义 JSON 格式或实现生成器。

参与编译的 `.inc` 测点清单放在 [sig](../sig/README.md)，协议映射和采集清单
放在 [mapping](../mapping/README.md)，目前手工维护。后续生成器将以本目录
的 JSON 为输入，更新这两类清单及对应通信配置，并校验名称、目标引用和地址覆盖。

仓库根目录的 `config/aclass_config.cmake` 继续负责模块和构建选项。
