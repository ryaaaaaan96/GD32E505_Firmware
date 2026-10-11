#!/usr/bin/env python3
"""验证调度、通知合并、队列公平性及静动态资源生命周期。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-poll-") as folder:
    for static, dynamic in ((1, 0), (0, 1), (1, 1)):
        exe = str(Path(folder) / f"poll-{static}-{dynamic}")
        command = [
            "cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
            "-pthread", "-fsanitize=undefined", "-fno-sanitize-recover=all",
            f"-DAPOLL_STATIC_ENABLE={static}",
            f"-DAPOLL_DYNAMIC_ENABLE={dynamic}",
            "-Iplatform/aLib/include", "-Iplatform/aOS/public", "-Ifunc/aPoll",
        ]
        subprocess.run(command + [
            "func/aPoll/aPoll.c", "tests/poll/test_poll.c",
            "tests/poll/wait_mock.c", "tests/modbus/os_mock.c", "-o", exe,
        ], cwd=root, check=True)
        subprocess.run([exe], check=True, timeout=20)
        obj = str(Path(folder) / f"poll-{static}-{dynamic}.o")
        subprocess.run(command + ["-c", "func/aPoll/aPoll.c", "-o", obj],
                       cwd=root, check=True)
        symbols = subprocess.check_output(["nm", obj], text=True)
        assert (" T aPollInitStatic" in symbols) == bool(static)
        assert (" T aPollCreate" in symbols) == bool(dynamic)
        if not dynamic:
            assert " U aOSAlloc" not in symbols
            assert " U aOSFree" not in symbols
