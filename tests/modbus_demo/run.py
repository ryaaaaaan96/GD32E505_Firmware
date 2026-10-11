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
    "tests/poll/wait_mock.c", "func/aPoll/aPoll.c",
    "app/protocol/FAN_modbus_master.c",
    "app/protocol/IDU_modbus_slave.c",
    "app/devices/rs485/rs485_device.c",
    "app/protocol/protocol_tables.c",
    "func/aModbus/aModbus.c", "func/aModbus/aModbus_bus.c",
    "func/aModbus/aModbus_rtu.c",
    "func/aModbus/nanoMODBUS/nanomodbus.c", "func/aBus/src/aBus.c",
]
includes = [
    "app", "app/protocol", "app/devices/rs485",
    "app/devices/system", "device/aDev_LED",
    "app/protocol/inc", "app/task/system", "app/task/test",
    "func/aPoll", "func/aModbus", "func/aModbus/nanoMODBUS", "func/aBus/include",
    "platform/aLib/include", "platform/aOS/public", "platform/aDrv/include",
    "device/aDev_usart/include",
]
flags = [
    "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-pthread", "-fno-pie", "-no-pie", "-fsanitize=undefined",
    "-fno-sanitize-recover=all", "-ffunction-sections", "-fdata-sections",
    "-Wl,--gc-sections", "-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld",
    "-Wl,--wrap=aModbusPlanExecute", "-Wl,--wrap=aModbusServerProcess",
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
    for minimum, maximum in ((5, 15), (7, 12)):
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            f"-DADRV_USART_INTERRUPT_PRIORITY_MIN={minimum}",
            f"-DADRV_USART_INTERRUPT_PRIORITY_MAX={maximum}",
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
            for prefix in ("AMODBUS", "APOLL", "ADEV_USART"):
                definitions += [f"-D{prefix}_STATIC_ENABLE={1 - dynamic}",
                                f"-D{prefix}_DYNAMIC_ENABLE={dynamic}"]
            definitions += [f"-DABUS_DYNAMIC_ENABLE={dynamic}"]
            subprocess.run(flags + definitions + sources + ["-o", executable],
                           cwd=root, check=True)
            subprocess.run([executable], check=True, timeout=20)
            for mode in ("startup", "clock", "task"):
                subprocess.run([executable, mode], check=True, timeout=20)
            print(f"master={master}, dynamic={dynamic}: passed")
    # 调整普通 C 配置，验证地址空洞、字段映射和手动断帧。
    fixture = Path(tmp) / "fixture"
    protocol = fixture / "app/protocol"
    shutil.copytree(root / "app/protocol", protocol)
    test_source = fixture / "tests/modbus_demo/test_demo.c"
    test_source.parent.mkdir(parents=True)
    shutil.copy(root / "tests/modbus_demo/test_demo.c", test_source)
    params = protocol / "sig/FAN_test.inc"
    polls = protocol / "FAN_modbus_master.c"
    original_polls = polls.read_text()
    split_flag = "        .flags = AMODBUS_POLL_FLAG_BREAK_BEFORE,\n"
    assert original_polls.count(split_flag) == 1
    polls.write_text(original_polls.replace(split_flag, "        .flags = 0U,\n"))
    mappings = protocol / "IDU_modbus_slave.c"
    # 测试副本增加稀疏保持寄存器和输入寄存器映射，无需外围地址段。
    sparse_maps = """    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 16U,
        .quantity = 2U,
        .flags = AMODBUS_ACCESS_READ,
        .target = {IDU_SIG_DEVICE_ID, IDU_SIG_TEST_001, AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 20U,
        .quantity = 2U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_001, FAN_TEST_001_U32},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },

"""
    input_maps = """
    {
        .area = AMODBUS_AREA_INPUT_REGISTERS,
        .address = 0U,
        .quantity = 2U,
        .flags = AMODBUS_ACCESS_READ,
        .target = {IDU_SIG_DEVICE_ID, IDU_SIG_TEST_001, AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },"""
    original_maps = mappings.read_text()
    group_marker = "    /* 本地测试控制值"
    assert original_maps.count(group_marker) == 1
    changed = original_maps.replace(group_marker, sparse_maps + group_marker)
    changed, count = re.subn(
        r"(static const aModbusBusMap_t maps\[\] = \{\n)(.*?)(^\};)",
        lambda match: match[1] + match[2] + input_maps + "\n" + match[3],
        changed, flags=re.M | re.S)
    assert count == 1
    mappings.write_text(changed)
    fixture_sources = [str(fixture / source) if source in (
        "tests/modbus_demo/test_demo.c", "app/protocol/FAN_modbus_master.c",
        "app/protocol/IDU_modbus_slave.c",
        "app/protocol/protocol_tables.c") else source for source in sources]
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
    print("extended configuration: fields/maps/holes/permissions/polls passed")
    # 整组映射把内存偏移作为协议偏移，字段描述调序必须被拒绝。
    original_params = params.read_text()
    changed, count = re.subn(
        r"(    ABUS_PARAM\(FAN_TEST_001_U32,[\s\S]*?\n    \))\n"
        r"(    ABUS_PARAM\(FAN_TEST_001_S32,[\s\S]*?\n    \))",
        r"\2\n\1", params.read_text())
    assert count == 1
    params.write_text(changed)
    for master in (0, 1):
        executable = str(Path(tmp) / f"invalid-layout-{master}")
        subprocess.run(fixture_flags + [f"-DAPP_MODBUS_MASTER_ENABLE={master}"]
                       + fixture_sources + ["-o", executable],
                       cwd=root, check=True)
        subprocess.run([executable, "layout"], check=True, timeout=20)
    params.write_text(original_params)
    print("invalid STRUCT parameter order rejected before port open")
    # 从站 U32 映射误填为一个寄存器，必须在打开串口前拒绝。
    valid_maps = mappings.read_text()
    map_length = "        .address = 0U,\n        .quantity = 2U,"
    assert map_length in valid_maps
    mappings.write_text(valid_maps.replace(
        map_length, map_length.replace("2U", "1U"), 1))
    executable = str(Path(tmp) / "invalid-map-length")
    subprocess.run(fixture_flags + ["-DAPP_MODBUS_MASTER_ENABLE=0"]
                   + fixture_sources + ["-o", executable],
                   cwd=root, check=True)
    subprocess.run([executable, "length"], check=True, timeout=20)
    print("invalid slave quantity rejected before port open")
    mappings.write_text(valid_maps)

    # 普通数组仍执行运行期校验，无效地址映射在打开串口前被拒绝。
    mappings.write_text(mappings.read_text().replace(".address = 16U",
                                               ".address = 1U"))
    executable = str(Path(tmp) / "overlap")
    subprocess.run(fixture_flags + ["-DAPP_MODBUS_MASTER_ENABLE=0"]
                   + fixture_sources + ["-o", executable],
                   cwd=root, check=True)
    subprocess.run([executable, "mapping"], check=True, timeout=20)
    print("invalid maps rejected before port open")

    # 显式协议长度与 U32 目标不匹配，必须在打开串口前拒绝。
    valid_polls = polls.read_text()
    assert ".quantity = 2U" in valid_polls
    polls.write_text(valid_polls.replace(".quantity = 2U", ".quantity = 1U", 1))
    executable = str(Path(tmp) / "invalid-poll-length")
    subprocess.run(fixture_flags + ["-DAPP_MODBUS_MASTER_ENABLE=1"]
                   + fixture_sources + ["-o", executable],
                   cwd=root, check=True)
    subprocess.run([executable, "length"], check=True, timeout=20)
    print("invalid master quantity rejected before port open")
    polls.write_text(valid_polls)

    # 无效主站目标在端口打开和任务创建前拒绝。
    polls.write_text(polls.read_text().replace(
        '.target = {FAN_SIG_DEVICE_ID,', '.target = {99U,'))
    executable = str(Path(tmp) / 'invalid-poll')
    subprocess.run(fixture_flags + ['-DAPP_MODBUS_MASTER_ENABLE=1']
                   + fixture_sources + ['-o', executable],
                   cwd=root, check=True)
    subprocess.run([executable, 'poll'], check=True, timeout=20)
    print('invalid master target rejected before port open')
