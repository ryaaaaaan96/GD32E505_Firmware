# aDataBase（FlashDB）

本模块使用 FlashDB 2.2.99 的 KVDB 功能，FlashDB 源码位于 `flashDB/`，采用 Apache-2.0 许可。

存储适配和 FlashDB 放在同一个 func 模块中，不单独保留当前只服务于 FlashDB 的
aMemory 层。存储链路为：

```text
FlashDB KVDB -> 本模块 FAL 适配 -> aDev_Flash25q -> aDrv QSPI
```

应用层先初始化 Flash25Q 设备，再显式绑定该句柄，然后使用 `param` 或 `log` 分区名
初始化 FlashDB：

```c
struct fdb_kvdb database = {0};
aDataBaseBindFlash25q(&flash_handle);
fdb_kvdb_init(&database, "param_db", "param", NULL, NULL);
```

设备绑定与分区策略由本模块维护。产品分区参数位于本模块的
`port/include/aDatabase_flash_layout.h`。FlashDB 的 FAL API 仍为上游要求的全局接口，绑定
操作必须在任何数据库实例初始化之前完成；当前 adapter 绑定一个 Flash25Q 实例。

当前示例应用没有目标 PCB 的 SQPI 引脚配置，因此只编译 FlashDB，不执行外部 Flash 数据库自检。
