#!/usr/bin/env python3
"""使用真实 ARM 工具链，验证协议角色/分配裁剪及固件最终链接。"""
from pathlib import Path
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
    "dynamic_server": (1, 0, 1, 0, 1),
    "disabled": (0, 0, 0, 0, 0),
}

with tempfile.TemporaryDirectory(prefix="aclass-modbus-build-") as directory:
    base = Path(directory)
    hook = base / "force_modbus.cmake"
    hook.write_text('''
function(force_modbus)
    if(TARGET aModbus)
        target_link_libraries("${PROJECT_NAME}" PRIVATE aModbus)
        if(AMODBUS_STATIC_ENABLE)
            target_link_options("${PROJECT_NAME}" PRIVATE
                "-Wl,-u,aModbusInitStatic" "-Wl,-u,aModbusDeInitStatic")
        endif()
        if(AMODBUS_DYNAMIC_ENABLE)
            target_link_options("${PROJECT_NAME}" PRIVATE
                "-Wl,-u,aModbusCreate" "-Wl,-u,aModbusDestroy")
        endif()
        if(AMODBUS_CLIENT_ENABLE)
            target_link_options("${PROJECT_NAME}" PRIVATE
                "-Wl,-u,aModbusClientRead" "-Wl,-u,aModbusClientWrite"
                "-Wl,-u,aModbusClientReadSig" "-Wl,-u,aModbusClientWriteSig")
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
        for feature, value in zip(("ENABLE", "STATIC_ENABLE", "DYNAMIC_ENABLE",
                                   "CLIENT_ENABLE", "SERVER_ENABLE"), values):
            lines += [f"set(AMODBUS_{feature} {'ON' if value else 'OFF'})"]
        config.write_text("\n".join(lines) + "\n")
        build = base / name
        commands = (
            ["cmake", "-S", str(root), "-B", str(build), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", f"-DARM_GCC_ROOT={toolchain}",
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
        if enabled:
            nm = str(Path(toolchain) / "bin/arm-none-eabi-nm")
            symbols = subprocess.check_output([nm, str(archive)], text=True)
            exported = {line.split()[-1] for line in symbols.splitlines()
                        if " T " in line}
            assert ("aModbusClientRead" in exported) == bool(client)
            assert ("aModbusServerProcess" in exported) == bool(server)
            assert ("aModbusCreate" in exported) == bool(dynamic)
            assert ("aModbusInitStatic" in exported) == bool(static)
        print(f"{name}: Release firmware build/link passed")
