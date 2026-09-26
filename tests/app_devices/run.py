#!/usr/bin/env python3
"""Application device lifecycle tests; hardware initializers are simulated."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-app-devices-") as directory:
    for scenario in ("NORMAL", "LED_FAILURE", "USART_FAILURE", "SHELL_OFF"):
        executable = str(Path(directory) / scenario)
        command = [
            "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-variable", f"-D{scenario}", "-DAPP_HAS_USART=1",
            "-DASHELL_ENABLED=" + ("0" if scenario == "SHELL_OFF" else "1"),
            "-Itests/usart/mocks", "-Idevice/aDev_LED", "-Idevice/aDev_usart",
            "-Iplatform/aDrv/include", "-Iplatform/aLib/include",
            "-Iapp/devices", "-Iapp/devices/led", "-Iapp/devices/usart",
            "-Iapp/task/system",
            "app/devices/led/app_led.c",
            "app/devices/usart/app_usart.c", "tests/app_devices/test_devices.c",
            "-o", executable,
        ]
        if os.environ.get("SANITIZE"):
            command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([executable], check=True)
        print(scenario, "passed")
