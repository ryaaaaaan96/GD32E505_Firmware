#!/usr/bin/env python3
"""使用真实 ARM 工具链，验证协议角色/分配裁剪及固件最终链接。"""
from pathlib import Path
import json
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
toolchain = os.environ.get(
    "ARM_GCC_ROOT",
    str(Path.home() / "Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3"))
profiles = {
    "both": (1, 1, 1, 1, 1),
    "static_client": (1, 1, 0, 1, 0),
    "dynamic_client": (1, 0, 1, 1, 0),
    "static_server": (1, 1, 0, 0, 1),
    "dynamic_server": (1, 0, 1, 0, 1),
    "disabled": (0, 0, 0, 0, 0),
    "core_no_usart": (1, 1, 1, 1, 1),
    "events_off_client": (1, 1, 1, 1, 1),
    "no_comm_server": (1, 1, 1, 0, 1),
    "core_no_bus": (1, 1, 1, 1, 1),
    "scheduler_only": (0, 0, 0, 0, 0),
    "static_comm_client": (1, 1, 1, 1, 0),
    "dynamic_comm_client": (1, 1, 0, 1, 0),
}

with tempfile.TemporaryDirectory(prefix="aclass-modbus-build-") as directory:
    base = Path(directory)
    hook = base / "force_modbus.cmake"
    hook.write_text('''
function(force_modbus)
    # 仅矩阵通过编译宏覆盖应用头默认值，产品不增加 CMake 角色开关。
    target_compile_definitions("${PROJECT_NAME}" PRIVATE
        APP_MODBUS_MASTER_ENABLE=$<BOOL:${TEST_MODBUS_MASTER_ENABLE}>)
    if(TARGET aModbus)
        file(WRITE "${CMAKE_BINARY_DIR}/modbus_api_probe.c"
            "#include <aModbus.h>\\n"
            "#if AMODBUS_STATIC_ENABLE\\n"
            "#include <aModbus_instance.h>\\n#endif\\n")
        add_library(modbus_api_probe OBJECT
            "${CMAKE_BINARY_DIR}/modbus_api_probe.c")
        target_link_libraries(modbus_api_probe PRIVATE aModbus)
        target_link_libraries("${PROJECT_NAME}" PRIVATE aModbus)
        if(AMODBUS_STATIC_ENABLE)
            target_link_options("${PROJECT_NAME}" PRIVATE
                "-Wl,-u,aModbusDeInitStatic")
        endif()
        if(AMODBUS_DYNAMIC_ENABLE)
            target_link_options("${PROJECT_NAME}" PRIVATE
                "-Wl,-u,aModbusDestroy")
        endif()
        foreach(role Client Server)
            string(TOUPPER "${role}" role_upper)
            if(AMODBUS_${role_upper}_ENABLE)
                if(AMODBUS_STATIC_ENABLE)
                    target_link_options("${PROJECT_NAME}" PRIVATE
                        "-Wl,-u,aModbus${role}InitStatic")
                endif()
                if(AMODBUS_DYNAMIC_ENABLE)
                    target_link_options("${PROJECT_NAME}" PRIVATE
                        "-Wl,-u,aModbus${role}Create")
                endif()
            endif()
        endforeach()
        if(AMODBUS_CLIENT_ENABLE)
            target_link_options("${PROJECT_NAME}" PRIVATE
                "-Wl,-u,aModbusClientRead" "-Wl,-u,aModbusClientWrite"
                "-Wl,-u,aModbusClientPrepareRead" "-Wl,-u,aModbusClientReadPrepared")
        endif()
        if(AMODBUS_SERVER_ENABLE)
            target_link_options("${PROJECT_NAME}" PRIVATE
                "-Wl,-u,aModbusServerProcess")
        endif()
    endif()
endfunction()
cmake_language(DEFER CALL force_modbus)
''')
    for name, values in profiles.items():
        enabled, static, dynamic, client, server = values
        config = base / f"{name}.cmake"
        lines = [f'include("{root}/config/aclass_config.cmake")']
        if name == "core_no_bus":
            lines += ["set(ABUS_ENABLE OFF)"]
        if name == "events_off_client":
            lines += ["set(ABUS_EVENT_ENABLE OFF)"]
        if name == "core_no_usart":
            lines += ["set(ASHELL_ENABLE OFF)",
                      "set(ADEV_USART_ENABLE OFF)"]
            for feature in ("INTERRUPT", "DIRECT", "ASYNC", "RS485",
                            "STATIC", "DYNAMIC"):
                lines += [f"set(ADEV_USART_{feature}_ENABLE OFF)"]
        if name.startswith("static_"):
            lines += ["set(ASHELL_ENABLE OFF)",
                      "set(ACOMM_DYNAMIC_ENABLE OFF)",
                      "set(ABUS_DYNAMIC_ENABLE OFF)",
                      "set(ADEV_USART_STATIC_ENABLE ON)",
                      "set(ADEV_USART_DYNAMIC_ENABLE OFF)"]
        if name.startswith("dynamic_"):
            lines += ["set(ACOMM_STATIC_ENABLE OFF)"]
        if name in ("disabled", "no_comm_server"):
            lines += ["set(ACOMM_ENABLE OFF)"]
        for feature, value in zip(("ENABLE", "STATIC_ENABLE", "DYNAMIC_ENABLE",
                                   "CLIENT_ENABLE", "SERVER_ENABLE"), values):
            lines += [f"set(AMODBUS_{feature} {'ON' if value else 'OFF'})"]
        config.write_text("\n".join(lines) + "\n")
        build = base / name
        commands = (
            ["cmake", "-S", str(root), "-B", str(build), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", f"-DARM_GCC_ROOT={toolchain}",
             "-DTEST_MODBUS_MASTER_ENABLE=" +
             ("ON" if name.endswith("client") else "OFF"),
             f"-DACLASS_CONFIG_FILE={config}",
             f"-DCMAKE_PROJECT_INCLUDE={hook}"],
            ["cmake", "--build", str(build), "--parallel", "4"],
        )
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
        archive = build / "lib/libaModbus.a"
        assert archive.exists() == bool(enabled)
        database = json.loads((build / "compile_commands.json").read_text())
        demo_enabled = bool(enabled) and name not in (
            "core_no_usart", "no_comm_server", "core_no_bus")
        for role, device in (("master", "FAN"), ("slave", "IDU")):
            present = any(Path(item["file"]).name == f"{device}_modbus_{role}.c"
                          for item in database)
            assert present == demo_enabled, (name, role)
        for source in ("rs485_device.c",):
            present = any(Path(item["file"]).name == source
                          for item in database)
            assert present == demo_enabled, (name, source)
        for source in ("protocol.c",):
            assert any(Path(item["file"]).name == source
                       for item in database), (name, source)
        assert not any(Path(item["file"]).name == "modbus_task.c"
                       for item in database)
        assert not (build / "lib/libaModbusUsart.a").exists()
        for item in database:
            if "/func/aModbus/" in item["file"]:
                assert "/device/" not in item["command"], item["command"]
                assert "/aDrv/" not in item["command"], item["command"]
                assert "/aBus/" not in item["command"], item["command"]

            if "/func/aComm/" in item["file"]:
                assert "/aModbus/" not in item["command"], item["command"]

                assert "/aBus/" not in item["command"], item["command"]
        nm = str(Path(toolchain) / "bin/arm-none-eabi-nm")
        comm_archive = build / "lib/libaComm.a"
        if comm_archive.exists():
            comm_symbols = subprocess.check_output(
                [nm, str(comm_archive)], text=True)
            assert not any("aModbus" in line or "aBus" in line
                           for line in comm_symbols.splitlines() if " U " in line)
        firmware = next((build / "bin").glob("*.elf"))
        symbols = subprocess.check_output([nm, str(firmware)], text=True)
        exported = {line.split()[-1] for line in symbols.splitlines()
                    if " T " in line}
        all_symbols = {line.split()[-1] for line in symbols.splitlines()
                       if len(line.split()) >= 3}
        for role, device in (("master", "FAN"), ("slave", "IDU")):
            expected = demo_enabled and (
                (role == "master") == name.endswith("client"))
            assert (f"{device}_modbus_{role}_config" in all_symbols) == expected, name
        assert "protocolInit" in exported, name
        if enabled:
            probe = next(item["command"] for item in database
                         if item["file"].endswith("modbus_api_probe.c"))
            assert ("/nanoMODBUS" in probe) == bool(static), probe
            assert ("-DNMBS_" in probe) == bool(static), probe
            symbols = subprocess.check_output([nm, str(archive)], text=True)
            exported = {line.split()[-1] for line in symbols.splitlines()
                        if " T " in line}
            assert ("aModbusClientRead" in exported) == bool(client)
            assert ("aModbusServerProcess" in exported) == bool(server)
            for role, role_enabled in (("Client", client), ("Server", server)):
                assert (f"aModbus{role}Create" in exported) == bool(dynamic and role_enabled)
                assert (f"aModbus{role}InitStatic" in exported) == bool(static and role_enabled)
            assert not any("aBus" in line
                           for line in symbols.splitlines() if " U " in line)
            assert "aModbusCreate" not in exported
            assert "aModbusInitStatic" not in exported
            assert not (build / "lib/libaModbusPoll.a").exists()
            if name == "no_comm_server":
                assert not (build / "lib/libaComm.a").exists()
        print(f"{name}: Release firmware build/link passed")
