#!/usr/bin/env python3
"""Application device lifecycle tests; hardware initializers are simulated."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-app-devices-") as directory:
    for scenario in ("NORMAL", "LED_FAILURE", "MUTEX_FAILURE",
                     "USART_FAILURE", "SHELL_FAILURE",
                     "SHELL_OFF", "CLEANUP_FAILURE", "SHELL_CLEANUP_FAILURE"):
        executable = str(Path(directory) / scenario)
        command = [
            "cc", "-std=c11",
            "-DADEV_LED_STATIC_ENABLE=1",
        "-DADEV_USART_STATIC_ENABLE=1", "-DADEV_USART_DYNAMIC_ENABLE=1", "-O2", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-variable", f"-D{scenario}",
            "-DASHELL_ENABLE=" + ("0" if scenario == "SHELL_OFF" else "1"),
            "-Itests/usart/mocks", "-Idevice/aDev_LED", "-Idevice/aDev_usart/include",
            "-Iplatform/aDrv/include", "-Iplatform/aLib/include",
            "-Iapp", "-Iapp/devices", "-Iapp/devices/system",
            "-Iapp/task/system", "-Ifunc/aShell/include",
            "app/devices/system/system_device.c", "tests/app_devices/test_devices.c",
            "-o", executable,
        ]
        if os.environ.get("SANITIZE"):
            command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        if scenario == "SHELL_CLEANUP_FAILURE":
            command[1:1] = ["-DSHELL_FAILURE", "-DCLEANUP_FAILURE"]
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([executable], check=True)
        print(scenario, "passed")

    # 真实控制台适配与 Shell；模拟有限 RX ring，验证丢字节后命令不会执行。
    executable = str(Path(directory) / "console_rx")
    command = [
        "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-pthread", "-fno-pie", "-no-pie",
        "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-DASHELL_ENABLE=1", "-DADEV_LED_STATIC_ENABLE=1",
        "-DADEV_USART_DYNAMIC_ENABLE=1",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-Wl,--fatal-warnings",
        "-Wl,-T,func/aShell/port/gcc/aShell_sections_host.ld",
        *[f"-I{folder}" for folder in (
            "platform/aOS/public", "platform/aLib/include", "platform/aDrv/include",
            "device/aDev_LED", "device/aDev_usart/include", "app/devices/system",
            "tests/log", "func/aShell", "func/aShell/include", "func/aShell/port",
            "func/aShell/nr_micro_shell/inc",
        )],
        "tests/app_devices/test_console_rx.c", "tests/log/os_mock.c",
        "app/devices/system/system_device.c",
        "func/aShell/src/aShell.c", "func/aShell/src/aShell_config.c",
        "func/aShell/src/aShell_output.c", "func/aShell/src/aShell_nr.c",
        "func/aShell/nr_micro_shell/src/nr_micro_shell_core.c", "-o", executable,
    ]
    if os.environ.get("SANITIZE"):
        command[1:1] = ["-fsanitize=address", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([executable], check=True, timeout=20)
