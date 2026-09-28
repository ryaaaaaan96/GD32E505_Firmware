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
