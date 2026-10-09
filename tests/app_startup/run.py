#!/usr/bin/env python3
"""验证初始化任务的执行顺序、自删除以及初始化失败路径。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-startup-") as directory:
    executable = str(Path(directory) / "test_startup")
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Iplatform/aDrv/include", "-Iplatform/aOS/public",
        "-Iplatform/aLib/include", "-Iapp/task/system",
        "tests/app_startup/test_startup.c", "-o", executable,
    ], cwd=root, check=True)
    subprocess.run([executable], check=True)
print("Application startup sequencing/self-delete/failure tests passed")

with tempfile.TemporaryDirectory(prefix="aclass-system-") as directory:
    executable = str(Path(directory) / "test_system")
    includes = ("platform/aDrv/include", "platform/aOS/public",
                "platform/aLib/include", "device/aDev_LED",
                "device/aDev_Flash25q", "app", "app/task/system",
                "app/devices/system",
                "app/data/sig", "app/task/sig", "func/aShell/include",
                "func/aMemory", "func/aDataBase", "func/aLog",
                "func/aBus/include", "app/data/modbus", "app/task/modbus")
    command = [
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        *["-I" + path for path in includes],
        *["-D" + feature + "=1" for feature in (
            "ASHELL_ENABLE", "APP_LOG_ENABLE", "ADEV_FLASH25Q_ENABLE",
            "AMEMORY_ENABLE", "APP_DATABASE_ENABLE", "ABUS_ENABLE",
            "APP_MODBUS_ENABLE")],
        "app/task/system/system_init.c", "tests/app_startup/test_system.c",
        "-o", executable,
    ]
    for master in (0, 1):
        subprocess.run(command + [f"-DAPP_MODBUS_MASTER_ENABLE={master}"],
                       cwd=root, check=True)
        subprocess.run([executable], check=True)
