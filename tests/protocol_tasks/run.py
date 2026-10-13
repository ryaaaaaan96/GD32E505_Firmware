#!/usr/bin/env python3
"""验证产品点表配置、清单扩展及不依赖任务创建的协议入口。"""
from pathlib import Path
import subprocess
import tempfile
import shutil

root = Path(__file__).resolve().parents[2]
flags = [
    "cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-fsanitize=undefined", "-fno-sanitize-recover=all",
    "-Iplatform/aLib/include", "-Iplatform/aOS/public",
    "-Ifunc/aBus/include", "-Ifunc/aModbus/include", "-Ifunc/aDataBase",
    "-Ifunc/aMemory", "-Iapp", "-Iapp/protocol",
    "-Iapp/protocol/inc", "-Iapp/task/test", "-Iapp/task/system",
]
with tempfile.TemporaryDirectory(prefix="aclass-protocol-tasks-") as folder:
    executable = str(Path(folder) / "tasks")
    # 配置装配不创建总线或任务；启动顺序由 app_startup 验证。
    # 点表即使配置持久化，也无需 FlashDB 私有头或 Flash 分区配置头。
    for bus, dynamic, database in ((1, 0, 0), (1, 1, 0), (0, 1, 0),
                                   (1, 0, 1), (1, 1, 1)):
        subprocess.run(flags + [
            f"-DABUS_ENABLE={bus}", "-DAPP_MODBUS_ENABLE=0",
            f"-DABUS_DYNAMIC_ENABLE={dynamic}",
            f"-DAPP_DATABASE_ENABLE={database}",
            f"-DADATABASE_DYNAMIC_ENABLE={dynamic}",
            "tests/protocol_tasks/test_init.c", "app/protocol/protocol.c",
            "app/protocol/protocol_tables.c",
            "-o", executable,
        ], cwd=root, check=True)
        subprocess.run([executable], check=True)
    # 只修改临时副本中的清单，验证新增测点不要求修改头文件或装配代码。
    fixture = Path(folder) / "protocol"
    shutil.copytree(root / "app/protocol/inc", fixture / "inc")
    shutil.copytree(root / "app/protocol/sig", fixture / "sig")
    shutil.copytree(root / "app/protocol/persistence", fixture / "persistence")
    for name in ("protocol.c", "protocol_tables.c"):
        shutil.copy(root / "app/protocol" / name, fixture / name)
    definition = fixture / "sig/IDU_test.inc"
    definition.write_text('''ABUS_SIG(SPARE, 900U, uint16_t,
    .type = ALIB_DATA_U16,

    .default_data = &(const uint16_t){7U}
)
''' + definition.read_text())
    selection = fixture / "persistence/sig_persist.inc"
    selection.write_text('PERSIST_SIG(IDU_SIG_DEVICE_ID, IDU_SIG_SPARE_KEY)\n'
                         + selection.read_text())
    for dynamic in (0, 1):
        subprocess.run(flags[:1] + ["-I" + str(fixture / "inc")]
                       + flags[1:] + [
            "-DABUS_ENABLE=1", "-DAPP_MODBUS_ENABLE=0", "-DTEST_ADDED_SIG=1",
            "-DAPP_DATABASE_ENABLE=1",
            f"-DADATABASE_DYNAMIC_ENABLE={dynamic}",
            f"-DABUS_DYNAMIC_ENABLE={dynamic}",
            "tests/protocol_tasks/test_init.c", str(fixture / "protocol.c"),
            str(fixture / "protocol_tables.c"),
            "-o", executable,
        ], cwd=root, check=True)
        subprocess.run([executable], check=True)
print("Protocol configuration/lifetime/disabled/list expansion passed")
