#!/usr/bin/env python3
"""验证真实 SPI 驱动配置预检查和板级软件片选。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-spi-") as tmp:
    binary = str(Path(tmp) / "spi")
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-Itests/spi/mocks", "-Iplatform/aDrv/src",
        "-Iplatform/aDrv/include", "-Iplatform/aLib/include",
        "tests/spi/test_spi.c", "platform/aDrv/src/spi/aDrv_spi.c",
        "platform/aDrv/src/gpio/aDrv_gpio.c", "-o", binary,
    ], cwd=root, check=True)
    subprocess.run([binary], check=True)
