#!/usr/bin/env python3
"""Run aOS backend regression tests: SANITIZE=1 python3 tests/aos/run.py."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-aos-") as directory:
  for test in ("test_init_work", "test_timer_notify", "test_task_exit", "test_task_create", "test_time"):
    executable = str(Path(directory) / test)
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-DAOS_WORKQUEUE_ENABLE=1",
        *(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
          if os.environ.get("SANITIZE") else []),
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-Itests/aos/mocks", "-Iplatform/aOS/public", "-Iplatform/aLib/include",
        f"tests/aos/{test}.c", "-o", executable,
    ], cwd=root, check=True)
    subprocess.run([executable], check=True)
