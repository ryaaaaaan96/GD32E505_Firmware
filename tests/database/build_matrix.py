#!/usr/bin/env python3
"""在独立构建目录验证数据库分配开关、存储依赖和关闭配置。"""
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
    "static": "set(ADATABASE_DYNAMIC_ENABLE OFF)",
    "dynamic": "set(ADATABASE_STATIC_ENABLE OFF)",
    "custom": "set(ADEV_FLASH25Q_ENABLE OFF)\nset(ADRV_MODULE_SPI_ENABLE OFF)",
    "disabled": "set(ADATABASE_ENABLE OFF)",
    "memory_off": "set(ADATABASE_ENABLE OFF)\nset(AMEMORY_ENABLE OFF)",
    "no_shell": "set(ASHELL_ENABLE OFF)",
    "metadata_only": ("set(ABUS_ENABLE OFF)\n"
                      "set(ABUS_STATIC_ENABLE OFF)\n"
                      "set(ABUS_DYNAMIC_ENABLE OFF)\n"
                      "set(AMODBUS_ENABLE OFF)"),
}
with tempfile.TemporaryDirectory(prefix="database-build-") as directory:
    base = Path(directory)
    force_link = base / "force.cmake"
    force_link.write_text('''function(database_force_link)
    if(TARGET aDataBase)
        file(WRITE "${CMAKE_BINARY_DIR}/database_api_probe.c"
            "#include <aDataBase.h>\\n"
            "#if ADATABASE_STATIC_ENABLE\\n"
            "#include <aDataBase_instance.h>\\n#endif\\n")
        add_library(database_api_probe OBJECT
            "${CMAKE_BINARY_DIR}/database_api_probe.c")
        target_link_libraries(database_api_probe PRIVATE aDataBase)
    endif()
    if(TARGET aDataBase AND NOT ADEV_FLASH25Q_ENABLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "-Wl,--whole-archive" aDataBase "-Wl,--no-whole-archive")
    endif()
endfunction()
cmake_language(DEFER CALL database_force_link)
''')
    for name, options in profiles.items():
        config = base / f"{name}.cmake"
        config.write_text(
            f'include("{root}/config/aclass_config.cmake")\n' + options + "\n")
        build = base / name
        commands = [
            ["cmake", "-S", str(root), "-B", str(build), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", f"-DARM_GCC_ROOT={toolchain}",
             f"-DACLASS_CONFIG_FILE={config}",
             f"-DCMAKE_PROJECT_INCLUDE={force_link}"],
            ["cmake", "--build", str(build), "-j", "4"],
        ]
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode or "warning:" in result.stdout + result.stderr:
                print(result.stdout, result.stderr)
                raise SystemExit(result.returncode or 1)
        nm = str(Path(toolchain) / "bin/arm-none-eabi-nm")
        elf = next((build / "bin").glob("*.elf"))
        symbols = subprocess.check_output([nm, str(elf)], text=True)
        assert not any(" fal_" in line for line in symbols.splitlines())
        if name != "disabled" and name != "memory_off":
            database = json.loads((build / "compile_commands.json").read_text())
            probe = next(item["command"] for item in database
                         if item["file"].endswith("database_api_probe.c"))
            assert ("/FlashDB/inc" in probe) == (name != "dynamic"), probe
            assert "-DABUS_LOCK_MODE=" not in probe, probe
            if name == "metadata_only":
                assert not (build / "lib/libaBus.a").exists()
        print(f"Database {name}: build/link passed; no FAL", flush=True)
