#!/usr/bin/env python3
"""Shell lifecycle, single-step processing and task-ownership regression."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-shell-") as directory:
    executable = str(Path(directory) / "shell")
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-Itests/shell/mocks", "-Ifunc/aShell", "-Iplatform/aOS/public",
               "-Iplatform/aLib/include", "tests/shell/test_shell.c",
               "func/aShell/aShell.c", "-o", executable]
    if os.environ.get("SANITIZE"):
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([executable], check=True)
    print("Shell lifecycle/task-ownership tests passed")
