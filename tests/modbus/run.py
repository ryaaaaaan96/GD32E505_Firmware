#!/usr/bin/env python3
"""验证真实 nanoMODBUS/aBus 链路及角色、分配、锁粒度的编译裁剪。"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
module = root / "func/aModbus"
sources = [module / "aModbus.c", module / "aModbus_bus.c",
           module / "nanoMODBUS/nanomodbus.c",
           root / "func/aBus/src/aBus.c",
           root / "tests/modbus/os_mock.c",
           root / "tests/modbus/test_modbus.c"]
common = ["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Wpedantic",
          "-Werror", "-pthread", "-fsanitize=undefined",
          "-fno-sanitize-recover=all", "-fno-pie", "-no-pie",
          "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
          "-Wl,--fatal-warnings",
          f"-Wl,-T,{root}/func/aBus/port/gcc/aBus_sections_host.ld",
          "-DABUS_STATIC_ENABLE=1", "-DABUS_DYNAMIC_ENABLE=1",
          "-DABUS_DEF_CHECK_ENABLE=1",
          "-DNMBS_SERVER_READ_FILE_RECORD_DISABLED",
          "-DNMBS_SERVER_WRITE_FILE_RECORD_DISABLED",
          "-DNMBS_SERVER_READ_WRITE_REGISTERS_DISABLED",
          "-DNMBS_SERVER_READ_DEVICE_IDENTIFICATION_DISABLED"]
if os.environ.get("SANITIZE"):
    common += ["-fsanitize=address", "-fno-omit-frame-pointer"]
for include in (module, module / "nanoMODBUS", root / "func/aBus/include",
                root / "platform/aLib/include", root / "platform/aOS/public"):
    common += ["-I", str(include)]

profiles = ((1, 1, 1, 1, 0), (1, 1, 1, 0, 1), (1, 1, 0, 1, 2),
            (1, 0, 1, 0, 0), (0, 1, 0, 1, 1))
with tempfile.TemporaryDirectory(prefix="aclass-modbus-") as directory:
    for client, server, static, dynamic, lock in profiles:
        name = f"modbus-{client}-{server}-{static}-{dynamic}-{lock}"
        flags = [f"-DAMODBUS_CLIENT_ENABLE={client}",
                 f"-DAMODBUS_SERVER_ENABLE={server}",
                 f"-DAMODBUS_STATIC_ENABLE={static}",
                 f"-DAMODBUS_DYNAMIC_ENABLE={dynamic}",
                 f"-DABUS_LOCK_MODE={lock}"]
        if not client:
            flags += ["-DNMBS_CLIENT_DISABLED"]
        if not server:
            flags += ["-DNMBS_SERVER_DISABLED"]
        executable = str(Path(directory) / name)
        subprocess.run(common + flags + [str(p) for p in sources]
                       + ["-o", executable], check=True)
        subprocess.run([executable], check=True, timeout=20)
        obj = str(Path(directory) / (name + ".o"))
        subprocess.run(common + flags + ["-c", str(module / "aModbus.c"),
                       "-o", obj], check=True)
        symbols = subprocess.check_output(["nm", obj], text=True)
        exported = {line.split()[-1] for line in symbols.splitlines()
                    if " T " in line}
        assert ("aModbusClientRead" in exported) == bool(client)
        assert ("aModbusServerProcess" in exported) == bool(server)
        assert ("aModbusCreate" in exported) == bool(dynamic)
        assert ("aModbusInitStatic" in exported) == bool(static)
        if not dynamic:
            assert " U aOSAlloc" not in symbols
            assert " U aOSFree" not in symbols
        print(f"{name}: protocol/data/feature checks passed")
    executable = str(Path(directory) / "rtu")
    subprocess.run(common + [
        "-DAMODBUS_CLIENT_ENABLE=1", "-DAMODBUS_SERVER_ENABLE=1",
        "-DAMODBUS_STATIC_ENABLE=1", "-DAMODBUS_DYNAMIC_ENABLE=1",
        str(module / "aModbus.c"), str(module / "aModbus_rtu.c"),
        str(root / "tests/modbus/os_mock.c"),
        str(root / "tests/modbus/test_rtu.c"), "-o", executable,
    ], check=True)
    subprocess.run([executable], check=True, timeout=20)
