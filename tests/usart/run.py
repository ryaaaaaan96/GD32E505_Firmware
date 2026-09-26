#!/usr/bin/env python3
"""Run USART host tests without hardware: python3 tests/usart/run.py."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-usart-") as directory:
    executable = str(Path(directory) / "test_rs485")
    command = [
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        *(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
          if os.environ.get("SANITIZE") else []),
        "-DADRV_USART_HAS_INTERRUPT=1", "-DADRV_USART_HAS_ASYNC=1",
        *[f"-DADEV_USART_HAS_{feature}=1" for feature in (
            "INTERRUPT", "DMA", "ASYNC", "RS485",
        )],
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-Itests/usart/mocks", "-Idevice/aDev_usart",
        "-Iplatform/aLib/include", "-Iplatform/aDrv/include",
        "tests/usart/test_rs485.c", "device/aDev_usart/aDev_usart.c",
        "device/aDev_usart/aDev_usart_rx.c",
        "device/aDev_usart/aDev_usart_async_rx.c",
        "device/aDev_usart/aDev_usart_dma_rx.c",
        "device/aDev_usart/aDev_usart_direct.c",
        "device/aDev_usart/aDev_usart_rs485.c", "-o", executable,
        "device/aDev_usart/aDev_usart_tx_queue.c",
    ]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([executable], check=True)

    fifo = str(Path(directory) / "test_fifo")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Iplatform/aLib/include", "tests/usart/test_fifo.c",
                    "-o", fifo], cwd=root, check=True)
    subprocess.run([fifo], check=True)
