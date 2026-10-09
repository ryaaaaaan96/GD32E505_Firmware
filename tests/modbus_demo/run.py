#!/usr/bin/env python3
"""验证板级 Demo 的主从模式、静动态实例及真实 Modbus/aBus 链路。"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
sources = [
    "tests/modbus_demo/test_demo.c", "tests/modbus/os_mock.c",
    "app/data/modbus/modbus_master.c",
    "app/data/modbus/modbus_slave.c",
    "app/devices/rs485/rs485_config.c",
    "app/task/modbus/modbus_task.c", "app/data/sig/sig_data.c",
    "func/aModbus/aModbus.c", "func/aModbus/aModbus_bus.c",
    "func/aModbus/aModbus_rtu.c", "func/aModbus/aModbus_rtu_usart.c",
    "func/aModbus/nanoMODBUS/nanomodbus.c", "func/aBus/src/aBus.c",
]
includes = [
    "app", "app/data/modbus", "app/devices/rs485", "app/task/modbus",
    "app/data/sig",
    "func/aModbus", "func/aModbus/nanoMODBUS", "func/aBus/include",
    "platform/aLib/include", "platform/aOS/public", "platform/aDrv/include",
    "device/aDev_usart/include",
]
flags = [
    "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-pthread", "-fno-pie", "-no-pie", "-fsanitize=undefined",
    "-fno-sanitize-recover=all", "-ffunction-sections", "-fdata-sections",
    "-Wl,--gc-sections", "-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld",
    "-DAPP_MODBUS_ENABLE=1", "-DASHELL_ENABLE=0",
    "-DABUS_STATIC_ENABLE=1", "-DABUS_LOCK_MODE=1",
    "-DADEV_USART_INTERRUPT_ENABLE=1", "-DADEV_USART_RS485_ENABLE=1",
    "-DNMBS_SERVER_READ_FILE_RECORD_DISABLED",
    "-DNMBS_SERVER_WRITE_FILE_RECORD_DISABLED",
    "-DNMBS_SERVER_READ_WRITE_REGISTERS_DISABLED",
    "-DNMBS_SERVER_READ_DEVICE_IDENTIFICATION_DISABLED",
    *["-I" + path for path in includes],
]
if os.environ.get("SANITIZE"):
    flags += ["-fsanitize=address", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="aclass-modbus-demo-") as tmp:
    routes = str(Path(tmp) / "routes")
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-Itests/modbus_demo/mocks", "-Iplatform/aDrv/src",
        "-Iplatform/aDrv/include", "-Iplatform/aLib/include",
        "tests/modbus_demo/test_routes.c", "platform/aDrv/src/aDrv_gpio.c",
        "platform/aDrv/src/usart/aDrv_usart.c", "-o", routes,
    ], cwd=root, check=True)
    subprocess.run([routes], check=True)
    for master in (0, 1):
        for dynamic in (0, 1):
            executable = str(Path(tmp) / f"demo-{master}-{dynamic}")
            definitions = [f"-DAPP_MODBUS_MASTER_ENABLE={master}"]
            for prefix in ("AMODBUS", "ADEV_USART"):
                definitions += [f"-D{prefix}_STATIC_ENABLE={1 - dynamic}",
                                f"-D{prefix}_DYNAMIC_ENABLE={dynamic}"]
            definitions += [f"-DABUS_DYNAMIC_ENABLE={dynamic}"]
            subprocess.run(flags + definitions + sources + ["-o", executable],
                           cwd=root, check=True)
            subprocess.run([executable], check=True, timeout=20)
            print(f"master={master}, dynamic={dynamic}: passed")
