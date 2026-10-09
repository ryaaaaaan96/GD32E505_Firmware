# 点表与协议配置

本目录用于存放 IDU、FAN 等设备的 CSV 或 JSON 配置，后续据此生成测点标识、
aBus 点表及 Modbus 映射、采集配置。

当前仅预留目录，配置格式和生成工具尚未实现；现有 C 配置继续参与编译。
格式确定后再加入实际配置文件，不在此维护另一份手写点表或寄存器说明。

生成的通信配置直接使用 aModbusServiceConfig_t；任务栈、优先级和轮询周期
由应用填写，任务由 protocol.c 创建。
后续生成器应统一输出 const 配置定义及 extern 声明，具体约定见
[配置对象的 extern 约定](../README.md#配置对象的-extern-约定)。

仓库根目录的 `config/aclass_config.cmake` 继续负责模块和构建选项。
