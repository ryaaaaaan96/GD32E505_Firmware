# aDataBase（FlashDB）

本模块使用 FlashDB 2.2.99 的 KVDB 功能，FlashDB 源码位于 `flashDB/`，采用 Apache-2.0 许可。

存储适配和 FlashDB 放在同一个 func 模块中，不单独保留当前只服务于 FlashDB 的
aMemory 层。存储链路为：

```text
FlashDB KVDB -> FAL 适配 -> aDataBaseStorage_t 操作表 -> 存储后端
```

应用层先初始化 Flash25Q 设备，再显式绑定该句柄，然后使用 `param` 或 `log` 分区名
初始化 FlashDB：

```c
#include "aDataBase_flash25q.h"
struct fdb_kvdb database = {0};
if (aDataBaseBindFlash25q(&flash_handle) == A_STATUS_OK) {
    fdb_kvdb_init(&database, "param_db", "param", NULL, NULL);
}
```

设备绑定与分区策略由本模块维护。产品分区参数位于产品的
`config/aDatabase_flash_layout.h`（通过 `ADATABASE_LAYOUT_FILE` 注入）。FlashDB 的 FAL API 仍为上游要求的全局接口，绑定
操作必须在任何数据库实例初始化之前完成。关闭所有数据库、停止在途调用后才能
调用 aDataBaseUnbindStorage；生命周期操作由应用串行管理。

config/aclass_config.cmake 中 ADATABASE_BACKEND=FLASH25Q 构建可选 aDataBaseFlash25q
适配目标，需要显式启用 ADEV_FLASH25Q_ENABLE 及 QSPI，使用它的 app 链接此目标。
ADATABASE_BACKEND=CUSTOM 只构建不依赖 Flash25Q 的 aDataBase 核心，应用通过
aDataBaseBindStorage 提供 read/write/erase、context 和存储几何参数。操作表会被复制，
context 必须保持有效到解除绑定。几何参数必须匹配当前 FAL 分区布局；绑定一个存储实例。

当前示例默认关闭数据库，也没有目标 PCB 的 SQPI 引脚配置，不执行外部 Flash 数据库自检。
