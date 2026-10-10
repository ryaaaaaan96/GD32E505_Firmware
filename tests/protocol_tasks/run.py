#!/usr/bin/env python3
"""验证协议统一入口、任务调度和部分启动失败语义。"""
from pathlib import Path
import subprocess
import tempfile
import shutil

root = Path(__file__).resolve().parents[2]
flags = [
    "cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-fsanitize=undefined", "-fno-sanitize-recover=all",
    "-Iplatform/aLib/include", "-Iplatform/aOS/public",
    "-Ifunc/aBus/include", "-Ifunc/aModbus", "-Iapp", "-Iapp/protocol",
    "-Iapp/protocol/inc", "-Iapp/task/sig", "-Iapp/task/system",
]
with tempfile.TemporaryDirectory(prefix="aclass-protocol-tasks-") as folder:
    executable = str(Path(folder) / "tasks")
    # 每次运行模拟一次开机，检查双点表装配及依赖失败传播；通信路径由 modbus_demo 覆盖。
    for bus, dynamic in ((1, 0), (1, 1), (0, 1)):
        subprocess.run(flags + [
            f"-DABUS_ENABLE={bus}", "-DAPP_MODBUS_ENABLE=0",
            f"-DABUS_DYNAMIC_ENABLE={dynamic}",
            "tests/protocol_tasks/test_init.c", "app/protocol/protocol.c",
            "-o", executable,
        ], cwd=root, check=True)
        failures = range(3 if bus else 1)
        for failure in failures:
            subprocess.run([executable, str(failure)], check=True)
    # 只修改临时副本中的清单，验证新增测点不要求修改头文件或装配代码。
    fixture = Path(folder) / "protocol"
    shutil.copytree(root / "app/protocol/inc", fixture / "inc")
    shutil.copytree(root / "app/protocol/sig", fixture / "sig")
    shutil.copy(root / "app/protocol/protocol.c", fixture / "protocol.c")
    definition = fixture / "sig/IDU_sig.inc"
    definition.write_text('''ABUS_SIG(SPARE, 900U,
    .type = ALIB_DATA_U16,
    .size = sizeof(uint16_t),
    .default_data = &(const uint16_t){7U}
)
''' + definition.read_text())
    for dynamic in (0, 1):
        subprocess.run(flags[:1] + ["-I" + str(fixture / "inc")]
                       + flags[1:] + [
            "-DABUS_ENABLE=1", "-DAPP_MODBUS_ENABLE=0", "-DTEST_ADDED_SIG=1",
            f"-DABUS_DYNAMIC_ENABLE={dynamic}",
            "tests/protocol_tasks/test_init.c", str(fixture / "protocol.c"),
            "-o", executable,
        ], cwd=root, check=True)
        subprocess.run([executable, "0"], check=True)
print("Protocol initialization/order/failure/disabled/list expansion passed")
