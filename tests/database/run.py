#!/usr/bin/env python3
"""Build the storage port without hardware/OS headers or libraries."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-storage-") as directory:
    executable = str(Path(directory) / "storage")
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-Ifunc/aDataBase", "-Ifunc/aDataBase/port/include",
               "-Iplatform/aLib/include", "tests/database/test_storage.c",
               "func/aDataBase/port/fal_storage_port.c", "-o", executable]
    if os.environ.get("SANITIZE"):
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([executable], check=True)
    print("Database storage isolation/lifecycle tests passed")
