#!/usr/bin/env python3
"""验证无 OS、无堆的 aMemory 核心。"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-memory-") as directory:
    output = str(Path(directory) / "test")
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
               "-fsanitize=undefined", "-fno-sanitize-recover=all",
               "-I", str(root / "func/aMemory"),
               "-I", str(root / "platform/aLib/include")]
    if os.environ.get("SANITIZE"):
        command += ["-fsanitize=address", "-fno-omit-frame-pointer"]
    subprocess.run(command + [str(root / "func/aMemory/aMemory.c"),
                   str(root / "tests/memory/test_memory.c"), "-o", output],
                   check=True)
    subprocess.run([output], check=True, timeout=20)
