#!/usr/bin/env python3
"""编译官方 EasyLogger，验证通用后端、并发及共享控制台输出链路。"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
module = root / "func/aLog"
common = [
    "cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-pthread", "-fsanitize=undefined", "-fno-sanitize-recover=all",
]
if os.environ.get("SANITIZE"):
    common += ["-fsanitize=address", "-fno-omit-frame-pointer",
               "-fno-pie", "-no-pie"]
for path in (module, module / "config", module / "EasyLogger/easylogger/inc",
             root / "platform/aLib/include", root / "platform/aOS/public",
             root / "tests/log"):
    common += ["-I", str(path)]
sources = [module / "aLog.c", module / "aLog_config.c",
           module / "port/elog_port.c",
           module / "EasyLogger/easylogger/src/elog.c",
           module / "EasyLogger/easylogger/src/elog_utils.c",
           root / "tests/log/os_mock.c"]

with tempfile.TemporaryDirectory(prefix="aclass-log-") as directory:
    for level, capacity in ((5, 256), (2, 256), (5, 512)):
        executable = str(Path(directory) / f"log-{level}-{capacity}")
        subprocess.run(common + ["-DALOG_ENABLE=1",
                       f"-DALOG_OUTPUT_LEVEL={level}",
                       f"-DALOG_LINE_BUFFER_SIZE={capacity}",
                       str(root / "tests/log/test_log.c")]
                       + [str(source) for source in sources]
                       + ["-o", executable], check=True)
        subprocess.run([executable], check=True, timeout=20)

    executable = str(Path(directory) / "shell")
    shell = root / "func/aShell"
    flags = ["-DALOG_ENABLE=1", "-DALOG_OUTPUT_LEVEL=5",
             "-DADEV_LED_STATIC_ENABLE=1", "-DADEV_USART_DYNAMIC_ENABLE=1",
             "-DASHELL_ENABLE=1", "-fno-pie", "-no-pie",
             "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
             "-Wl,--fatal-warnings",
             f"-Wl,-T,{shell}/port/gcc/aShell_sections_host.ld"]
    for path in (shell, shell / "include", shell / "port",
                 shell / "nr_micro_shell/inc", root / "app/devices/system",
                 root / "app/task/system", root / "device/aDev_LED",
                 root / "device/aDev_usart/include",
                 root / "platform/aDrv/include"):
        flags += ["-I", str(path)]
    shell_sources = [shell / "src" / name for name in
                     ("aShell.c", "aShell_config.c", "aShell_output.c",
                      "aShell_nr.c")]
    shell_sources += [shell / "nr_micro_shell/src/nr_micro_shell_core.c",
                      root / "app/devices/system/system_device.c",
                      root / "app/devices/system/log_config.c",
                      root / "app/task/system/log_service.c",
                      root / "app/task/system/log_command.c",
                      root / "tests/log/test_shell.c"]
    for capacity in (256, 512):
        subprocess.run(common + flags
                       + [f"-DALOG_LINE_BUFFER_SIZE={capacity}"]
                       + [str(source) for source in sources + shell_sources]
                       + ["-o", executable], check=True)
        subprocess.run([executable], check=True, timeout=20)

    executable = str(Path(directory) / "stub")
    subprocess.run(common + ["-DALOG_ENABLE=0", "-DALOG_OUTPUT_LEVEL=5",
                   "-DALOG_LINE_BUFFER_SIZE=256",
                   str(module / "aLog_config.c"),
                   str(module / "aLog_stub.c"),
                   str(root / "tests/log/test_stub.c"), "-o", executable],
                   check=True)
    subprocess.run([executable], check=True, timeout=20)
    symbols = subprocess.check_output(["nm", executable], text=True)
    assert "elog_output" not in symbols and "aOSMutexCreate" not in symbols
    print("aLog: disabled macros skip argument evaluation and OS objects")
