#!/usr/bin/env python3
"""验证板级 Demo 的主从模式、静动态实例及真实 Modbus/aBus 链路。"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
sources = [
    "tests/modbus_demo/test_demo.c", "tests/modbus/os_mock.c",
    "app/protocol/FAN_modbus_master.c",
    "app/protocol/IDU_modbus_slave.c",
    "app/devices/rs485/rs485_device.c",
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
    # 仅修改清单：字段调序、多地址段/区域、地址空洞和两个采集项。
    fixture = Path(tmp) / "fixture"
    protocol = fixture / "app/protocol"
    shutil.copytree(root / "app/protocol", protocol)
    test_source = fixture / "tests/modbus_demo/test_demo.c"
    test_source.parent.mkdir(parents=True)
    shutil.copy(root / "tests/modbus_demo/test_demo.c", test_source)
    params = protocol / "sig/FAN_sig.inc"
    changed, count = re.subn(
        r"(    ABUS_PARAM\(FAN_MOTOR_SPEED,[\s\S]*?\n    \))\n"
        r"(    ABUS_PARAM\(FAN_MOTOR_TEMPERATURE,[\s\S]*?\n    \))",
        r"\2\n\1", params.read_text())
    assert count == 1
    params.write_text(changed)
    polls = protocol / "mapping/FAN_modbus_master.inc"
    original_polls = polls.read_text()
    polls.write_text(original_polls + original_polls.replace(
        "AMODBUS_POLL(SPEED,", "AMODBUS_POLL(SPEED_RETRY,").replace(
        "500U", "501U"))
    ranges = protocol / "mapping/IDU_modbus_slave.inc"
    ranges.write_text(ranges.read_text() +
                      (root / "tests/modbus_demo/extra_ranges.inc").read_text())
    fixture_sources = [str(fixture / source) if source in (
        "tests/modbus_demo/test_demo.c", "app/protocol/FAN_modbus_master.c",
        "app/protocol/IDU_modbus_slave.c") else source for source in sources]
    fixture_flags = flags[:1] + ["-I" + str(protocol / "inc")] + flags[1:] + [
        "-DTEST_EXTENDED_LISTS=1", "-DABUS_DYNAMIC_ENABLE=1",
        "-DAMODBUS_STATIC_ENABLE=0", "-DAMODBUS_DYNAMIC_ENABLE=1",
        "-DADEV_USART_STATIC_ENABLE=0", "-DADEV_USART_DYNAMIC_ENABLE=1",
    ]
    for master in (0, 1):
        executable = str(Path(tmp) / f"extended-{master}")
        subprocess.run(fixture_flags + [f"-DAPP_MODBUS_MASTER_ENABLE={master}"]
                       + fixture_sources + ["-o", executable],
                       cwd=root, check=True)
        subprocess.run([executable], check=True, timeout=20)
    print("extended lists: fields/ranges/holes/permissions/polls passed")
    # 生成方式不绕过运行期校验，重叠地址段仍在打开串口前被拒绝。
    ranges.write_text(ranges.read_text().replace(".address = 16U",
                                               ".address = 1U"))
    executable = str(Path(tmp) / "overlap")
    subprocess.run(fixture_flags + ["-DAPP_MODBUS_MASTER_ENABLE=0"]
                   + fixture_sources + ["-o", executable],
                   cwd=root, check=True)
    subprocess.run([executable, "mapping"], check=True, timeout=20)
    print("overlapping generated ranges rejected")
