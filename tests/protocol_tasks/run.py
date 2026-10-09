#!/usr/bin/env python3
"""验证协议统一入口、任务调度和部分启动失败语义。"""
from pathlib import Path
import subprocess
import tempfile

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
        failures = range(5 if bus else 1)
        for failure in failures:
            subprocess.run([executable, str(failure)], check=True)
print("Protocol initialization/order/failure/disabled tests passed")
