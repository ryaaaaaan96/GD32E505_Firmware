#!/usr/bin/env python3
"""验证故障记录校验、基础/FPU 帧边界；异常入口须通过 ARM 构建与板上验证。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-fault-") as temporary:
    target = str(Path(temporary) / "fault")
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-Iplatform/aDrv/include", "-Iplatform/aLib/include", "-Itests/fault/mocks",
        "tests/fault/test_fault.c", "-o", target,
    ], cwd=root, check=True)
    subprocess.run([target], check=True)
print("Fault record/checksum/frame bounds passed")
