#!/usr/bin/env python3
"""验证真实 nanoMODBUS/aBus 链路及角色、分配、锁粒度的编译裁剪。"""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
module = root / "func/aModbus"
sources = [
    root / "func/aModbus/src/aModbus_mapping.c",
    root / "func/aModbus/src/aModbus_mapping_data.c",
    root / "func/aComm/src/aComm_table.c",
    root / "func/aComm/src/aComm.c",
    root / "func/aComm/src/aComm_channel.c",
    root / "func/aModbus/src/aModbus_endpoint.c",
    root / "func/aComm/src/aComm_endpoint.c",
    root / "func/aComm/src/aComm_binding.c",
    root / "func/aBus/src/aBus_endpoint.c",
    module / "src/aModbus.c", module / "src/aModbus_client.c",
    module / "src/aModbus_server.c", module / "nanoMODBUS/nanomodbus.c",
    root / "tests/comm/wait_mock.c", root / "func/aBus/src/aBus.c",
    root / "tests/modbus/os_mock.c", root / "tests/modbus/test_modbus.c",
]
common = ["cc", "-std=c11", "-DABUS_EVENT_ENABLE=0", "-O1", "-Wall", "-Wextra", "-Wpedantic",
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
for include in (module / "include", module / "src",
                root / "func/aComm/include", root / "func/aComm/src",
                root / "func/aComm/include",
                module / "nanoMODBUS", root / "func/aBus/include",
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
                 f"-DACOMM_STATIC_ENABLE={static}",
                 f"-DACOMM_DYNAMIC_ENABLE={dynamic}",
                 f"-DABUS_LOCK_MODE={lock}"]
        if not client:
            flags += ["-DNMBS_CLIENT_DISABLED"]
        if not server:
            flags += ["-DNMBS_SERVER_DISABLED"]
        executable = str(Path(directory) / name)
        subprocess.run(common + flags + [str(p) for p in sources]
                       + ["-o", executable], check=True)
        subprocess.run([executable], check=True, timeout=20)
        if client and server:
            struct_test = str(Path(directory) / (name + "-struct"))
            subprocess.run(common + flags + [str(p) for p in sources[:-1]]
                           + [str(root / "tests/modbus/test_struct.c"),
                              "-o", struct_test], check=True)
            subprocess.run([struct_test], check=True, timeout=20)
        if server:
            routing = str(Path(directory) / (name + "-routing"))
            subprocess.run(common + flags + [str(p) for p in sources[:-1]]
                           + [str(root / "tests/modbus/test_routing.c"),
                              "-o", routing], check=True)
            subprocess.run([routing], check=True, timeout=20)
        symbols = subprocess.check_output(["nm", executable], text=True)
        # 完整角色入口裁剪由 ARM build_matrix 检查；此处覆盖行为和无堆变体。
        print(f"{name}: protocol/data/feature checks passed")
    # 适配器事件绑定使用应用队列，不链接 aComm 通用调度实现。
    role_common = [flag for flag in common if flag != "-DABUS_EVENT_ENABLE=0"]
    role_sources = [source for source in sources[:-1]
                    if source.name not in ("aComm.c", "wait_mock.c")]
    for dynamic in (0, 1):
        executable = str(Path(directory) / f"roles-{dynamic}")
        subprocess.run(role_common + [
            "-DABUS_EVENT_ENABLE=1", "-DAMODBUS_CLIENT_ENABLE=1",
            "-DAMODBUS_SERVER_ENABLE=1", "-DAMODBUS_STATIC_ENABLE=1",
            f"-DAMODBUS_DYNAMIC_ENABLE={dynamic}",
            *[str(source) for source in role_sources],
            str(root / "func/aBus/src/aBus_event.c"),
            str(root / "tests/modbus/test_roles.c"), "-o", executable,
        ], check=True)
        subprocess.run([executable], check=True, timeout=20)
    executable = str(Path(directory) / "rtu")
    subprocess.run(common + [
        "-DAMODBUS_CLIENT_ENABLE=1", "-DAMODBUS_SERVER_ENABLE=1",
        "-DAMODBUS_STATIC_ENABLE=1", "-DAMODBUS_DYNAMIC_ENABLE=1",
        str(module / "src/aModbus.c"),
        str(root / "func/aComm/src/aComm_channel.c"),
        str(root / "tests/modbus/os_mock.c"),
        str(root / "tests/modbus/test_rtu.c"), "-o", executable,
    ], check=True)
    subprocess.run([executable], check=True, timeout=20)
    for static, dynamic in ((1, 0), (0, 1)):
        executable = str(Path(directory) / f"mapping-{static}-{dynamic}")
        subprocess.run(common + [
            f"-DACOMM_STATIC_ENABLE={static}",
            f"-DACOMM_DYNAMIC_ENABLE={dynamic}",
            *[str(p) for p in sources[:-1]],
            str(root / "tests/modbus/test_mapping.c"), "-o", executable,
        ], check=True)
        subprocess.run([executable], check=True, timeout=20)
    for lock in (1, 2):
        executable = str(Path(directory) / f"consistency-{lock}")
        subprocess.run(common + [
            "-DAMODBUS_CLIENT_ENABLE=1", "-DAMODBUS_SERVER_ENABLE=1",
            "-DAMODBUS_STATIC_ENABLE=1", "-DAMODBUS_DYNAMIC_ENABLE=1",
            f"-DABUS_LOCK_MODE={lock}", "-Wl,--wrap=aOSMutexUnlock",
            *[str(p) for p in sources[:-1]],
            str(root / "tests/modbus/test_consistency.c"),
            "-o", executable,
        ], check=True)
        subprocess.run([executable], check=True, timeout=20)
