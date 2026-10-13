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
            "func/aComm/src/aComm.c", "tests/comm/test_comm.c",
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

        mapping = str(Path(folder) / f"mapping-{static}-{dynamic}")
        subprocess.run(command + [
            "func/aComm/src/aComm_endpoint.c",
            *[str(p.relative_to(root)) for p in
              (root / "func/aComm/src").glob("aComm_mapping*.c")],
            "tests/comm/test_mapping.c", "tests/modbus/os_mock.c", "-o", mapping,
        ], cwd=root, check=True)
        subprocess.run([mapping], check=True, timeout=20)
