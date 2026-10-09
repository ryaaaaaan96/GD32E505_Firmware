#!/usr/bin/env python3
"""验证板级 Demo 的主从模式、静动态实例及真实 Modbus/aBus 链路。"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
sources = [
    "tests/modbus_demo/test_demo.c", "tests/modbus/os_mock.c",
    "app/protocol/FAN_modbus_master.c",
    "app/protocol/IDU_modbus_slave.c",
    "app/devices/rs485/rs485_device.c",
    "app/protocol/IDU_sig_table.c", "app/protocol/FAN_sig_table.c",
    "app/task/system/data_bus_service.c",
    "func/aModbus/aModbus.c", "func/aModbus/aModbus_bus.c",
    "func/aModbus/aModbus_rtu.c",
    "func/aModbus/nanoMODBUS/nanomodbus.c", "func/aBus/src/aBus.c",
]
includes = [
    "app", "app/protocol", "app/devices/rs485",
    "app/protocol/inc", "app/task/system", "app/task/sig",
    "func/aModbus", "func/aModbus/nanoMODBUS", "func/aBus/include",
    "platform/aLib/include", "platform/aOS/public", "platform/aDrv/include",
    "device/aDev_usart/include",
]
flags = [
    "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-pthread", "-fno-pie", "-no-pie", "-fsanitize=undefined",
    "-fno-sanitize-recover=all", "-ffunction-sections", "-fdata-sections",
    "-Wl,--gc-sections", "-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld",
    "-Wl,--wrap=aModbusClientReadSig", "-Wl,--wrap=aModbusServerProcess",
    "-DAPP_MODBUS_ENABLE=1", "-DABUS_ENABLE=1", "-DASHELL_ENABLE=0",
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
        "tests/modbus_demo/test_routes.c", "platform/aDrv/src/gpio/aDrv_gpio.c",
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
            for mode in ("startup", "clock", "task"):
                subprocess.run([executable, mode], check=True, timeout=20)
            print(f"master={master}, dynamic={dynamic}: passed")
    # 两项采集共用同一目标，校验失败推进、BUSY 重入及轮转顺序。
    executable = str(Path(tmp) / "poll-list")
    poll_sources = [source for source in sources
                    if not source.endswith("FAN_modbus_master.c")]
    subprocess.run(flags + [
        "-DAPP_MODBUS_MASTER_ENABLE=1", "-DABUS_DYNAMIC_ENABLE=1",
        "-DAMODBUS_STATIC_ENABLE=0", "-DAMODBUS_DYNAMIC_ENABLE=1",
        "-DADEV_USART_STATIC_ENABLE=0", "-DADEV_USART_DYNAMIC_ENABLE=1",
    ] + poll_sources + ["tests/modbus_demo/test_poll_config.c",
                        "-o", executable], cwd=root, check=True)
    subprocess.run([executable], check=True, timeout=20)
    print("multiple polls: progress/error/reentry passed")
