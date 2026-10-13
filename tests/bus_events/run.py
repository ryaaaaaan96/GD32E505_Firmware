#!/usr/bin/env python3
"""验证静动态事件状态、交集、多播接收/完成及无阻塞通知。"""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="abus-events-") as folder:
    for lock in (0, 1, 2):
        for dynamic in (0, 1):
            exe = str(Path(folder) / f"events-{lock}-{dynamic}")
            flags = ["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                "-Wpedantic", "-pthread", "-Wl,--wrap=aOSMutexUnlock",
                "-fsanitize=undefined",
                "-fno-sanitize-recover=all", "-fno-pie", "-no-pie",
                "-Ifunc/aBus/include", "-Iplatform/aLib/include",
                "-Iplatform/aOS/public", f"-DABUS_LOCK_MODE={lock}",
                f"-DABUS_DYNAMIC_ENABLE={dynamic}",
                "-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld"]
            if os.environ.get("SANITIZE"):
                flags += ["-fsanitize=address", "-fno-omit-frame-pointer"]
            subprocess.run(flags + ["func/aBus/src/aBus.c",
                "func/aBus/src/aBus_event.c", "tests/modbus/os_mock.c",
                "tests/bus_events/test_events.c", "-o", exe], cwd=root,
                check=True)
            subprocess.run([exe], check=True, timeout=15)
            print(f"aBus events: lock={lock}, dynamic={dynamic} passed")
