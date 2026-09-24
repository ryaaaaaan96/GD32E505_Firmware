#!/usr/bin/env python3
"""Run USART host tests without hardware: python3 tests/usart/run.py."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-usart-") as directory:
    executable = str(Path(directory) / "test_rs485")
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-Itests/usart/mocks", "-Idevice/aDev_usart",
        "-Iplatform/aLib/include", "-Iplatform/aDrv/include",
        "tests/usart/test_rs485.c", "device/aDev_usart/aDev_usart.c",
        "device/aDev_usart/aDev_usart_rx.c",
        "device/aDev_usart/aDev_usart_direct.c",
        "device/aDev_usart/aDev_usart_rs485.c", "-o", executable,
    ], cwd=root, check=True)
    subprocess.run([executable], check=True)
