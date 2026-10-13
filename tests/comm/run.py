#!/usr/bin/env python3
"""验证调度、通知合并、队列公平性及静动态资源生命周期。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-comm-") as folder:
    for static, dynamic in ((1, 0), (0, 1), (1, 1)):
        exe = str(Path(folder) / f"comm-{static}-{dynamic}")
        command = [
            "cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
            "-pthread",
            "-Wl,--wrap=aOSWaitObjectNotify", "-fsanitize=undefined",
            "-fno-sanitize-recover=all",
            f"-DACOMM_STATIC_ENABLE={static}",
            f"-DACOMM_DYNAMIC_ENABLE={dynamic}",
            "-Iplatform/aLib/include", "-Iplatform/aOS/public", "-Ifunc/aComm/include",
        ]
        subprocess.run(command + [
            "func/aComm/src/aComm.c", "func/aComm/src/aComm_table.c",
            "tests/comm/test_comm.c",
            "tests/comm/wait_mock.c", "tests/modbus/os_mock.c", "-o", exe,
        ], cwd=root, check=True)
        subprocess.run([exe], check=True, timeout=20)
        obj = str(Path(folder) / f"comm-{static}-{dynamic}.o")
        subprocess.run(command + ["-c", "func/aComm/src/aComm.c", "-o", obj],
                       cwd=root, check=True)
        symbols = subprocess.check_output(["nm", obj], text=True)
        assert (" T aCommInitStatic" in symbols) == bool(static)
        assert (" T aCommCreate" in symbols) == bool(dynamic)
        if not dynamic:
            assert " U aOSAlloc" not in symbols
            assert " U aOSFree" not in symbols

        # 不链接协议栈、aBus 或适配器，也不添加它们的头文件搜索路径。
        channel = str(Path(folder) / f"channel-{static}-{dynamic}")
        subprocess.run(command + [
            "-DACOMM_CHANNEL_FRAME_SIZE=16", "-DACOMM_CHANNEL_FRAME_COUNT=2",
            "func/aComm/src/aComm_channel.c", "tests/comm/test_channel.c",
            "tests/modbus/os_mock.c", "-o", channel,
        ], cwd=root, check=True)
        subprocess.run([channel], check=True, timeout=20)

        table = str(Path(folder) / f"table-{static}-{dynamic}")
        subprocess.run([flag for flag in command
                        if flag != "-Wl,--wrap=aOSWaitObjectNotify"] + [
            "func/aComm/src/aComm.c", "func/aComm/src/aComm_table.c",
            "tests/comm/test_table.c", "tests/comm/wait_mock.c",
            "tests/modbus/os_mock.c", "-o", table,
        ], cwd=root, check=True)
        subprocess.run([table], check=True, timeout=20)

        registration = str(Path(folder) / f"registration-{static}-{dynamic}")
        subprocess.run([flag for flag in command
                        if flag != "-Wl,--wrap=aOSWaitObjectNotify"] + [
            "func/aComm/src/aComm.c", "func/aComm/src/aComm_table.c",
            "tests/comm/test_registration.c", "tests/comm/wait_mock.c",
            "tests/modbus/os_mock.c", "-o", registration,
        ], cwd=root, check=True)
        subprocess.run([registration], check=True, timeout=20)

        binding = str(Path(folder) / f"binding-{static}-{dynamic}")
        subprocess.run([flag for flag in command
                        if flag != "-Wl,--wrap=aOSWaitObjectNotify"] + [
            "func/aComm/src/aComm_endpoint.c",
            "func/aComm/src/aComm_binding.c", "tests/comm/test_binding.c",
            "-o", binding,
        ], cwd=root, check=True)
        subprocess.run([binding], check=True, timeout=20)
