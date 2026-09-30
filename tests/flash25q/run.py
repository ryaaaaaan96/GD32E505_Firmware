#!/usr/bin/env python3
"""Run unchanged SFUD and project SPI port against a byte-level NOR model."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
module = root / "device/aDev_Flash25q"
with tempfile.TemporaryDirectory(prefix="flash25q-") as tmp:
    for static, dynamic in ((1, 0), (0, 1), (1, 1)):
        exe = Path(tmp) / f"test-{static}-{dynamic}"
        cmd = ["cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
               "-g", "-fsanitize=undefined", "-fno-sanitize-recover=all",
               f"-DADEV_FLASH25Q_STATIC_ENABLE={static}",
               f"-DADEV_FLASH25Q_DYNAMIC_ENABLE={dynamic}"]
        for directory in (module, module / "config", module / "SFUD/sfud/inc",
                          root / "platform/aDrv/include",
                          root / "platform/aLib/include",
                          root / "platform/aOS/public"):
            cmd += ["-I", str(directory)]
        cmd += [str(root / "tests/flash25q/test_flash25q.c")]
        cmd += [str(module / file) for file in (
            "aDev_flash25q.c", "port/aDev_flash25q_spi.c",
            "port/aDev_flash25q_sfud.c", "SFUD/sfud/src/sfud.c",
            "SFUD/sfud/src/sfud_sfdp.c")]
        subprocess.run(cmd + ["-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=15)
