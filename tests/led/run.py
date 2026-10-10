#!/usr/bin/env python3
"""验证 LED/GPIO 语义、对象生命周期与分配接口裁剪。"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
flags = [
    "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
    "-fsanitize=undefined", "-fno-sanitize-recover=all",
    "-Idevice/aDev_LED", "-Iplatform/aDrv/include",
    "-Iplatform/aDrv/src", "-Iplatform/aLib/include",
    "-Itests/modbus_demo/mocks",
]
if os.environ.get("SANITIZE"):
    flags += ["-fsanitize=address", "-fno-omit-frame-pointer"]

with tempfile.TemporaryDirectory(prefix="aclass-led-") as directory:
    for static, dynamic, disable in (
        (1, 0, None), (0, 1, None), (1, 1, None),
        (1, 0, 1), (1, 0, 0),
    ):
        name = f"static_{static}_dynamic_{dynamic}_swd_{disable}"
        obj = str(Path(directory) / (name + ".o"))
        binary = str(Path(directory) / name)
        options = flags + [
            f"-DADEV_LED_STATIC_ENABLE={static}",
            f"-DADEV_LED_DYNAMIC_ENABLE={dynamic}",
        ]
        if disable is not None:
            options += [f"-DADRV_GPIO_SWD_PROTECT_DISABLE={disable}"]
        # 纯静态实现不提供 aOS 头路径，防止意外引入 OS 依赖。
        if dynamic:
            options += ["-Iplatform/aOS/public"]
        subprocess.run([
            "cc", *options, "-c", "device/aDev_LED/aDev_led.c", "-o", obj,
        ], cwd=root, check=True)
        symbols = subprocess.check_output(["nm", obj], text=True)
        assert (" T aDevLedInitStatic" in symbols) == bool(static)
        assert (" T aDevLedCreate" in symbols) == bool(dynamic)
        assert (" T aDevLedDestroy" in symbols) == bool(dynamic)
        if not dynamic:
            assert " U aOS" not in symbols
        subprocess.run([
            "cc", *options, "tests/led/test_led.c",
            "platform/aDrv/src/gpio/aDrv_gpio.c", obj, "-o", binary,
        ], cwd=root, check=True)
        subprocess.run([binary], check=True)
        print(name, "passed")
