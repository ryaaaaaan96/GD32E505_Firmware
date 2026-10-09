#!/usr/bin/env python3
"""验证真实 DMA 驱动和 USART DMA 适配，包含 ISR/查询交错。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-dma-") as tmp:
    binary = str(Path(tmp) / "dma")
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-DADRV_USART_DMA_ENABLE=1",
        "-Itests/dma/mocks", "-Iplatform/aDrv/include",
        "-Iplatform/aDrv/src", "-Iplatform/aLib/include",
        "tests/dma/test_dma.c", "platform/aDrv/src/dma/aDrv_dma.c",
        "platform/aDrv/src/usart/aDrv_usart_dma.c", "-o", binary,
    ], cwd=root, check=True)
    subprocess.run([binary], check=True)
