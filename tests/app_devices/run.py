#!/usr/bin/env python3
"""Application device lifecycle tests; hardware initializers are simulated."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-app-devices-") as directory:
    for scenario in ("NORMAL", "LED_FAILURE", "USART_FAILURE", "SHELL_FAILURE",
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
