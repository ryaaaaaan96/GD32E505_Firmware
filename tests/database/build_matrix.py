#!/usr/bin/env python3
"""在独立构建目录验证数据库分配开关、后端选择和关闭配置。"""
from pathlib import Path
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
    "custom": "set(ADATABASE_BACKEND CUSTOM)\n"
              "set(ADEV_FLASH25Q_ENABLE OFF)\nset(ADRV_MODULE_SPI_ENABLE OFF)",
    "disabled": "set(ADATABASE_ENABLE OFF)",
    "no_shell": "set(ASHELL_ENABLE OFF)",
}
with tempfile.TemporaryDirectory(prefix="database-build-") as directory:
    base = Path(directory)
    force_link = base / "force.cmake"
    force_link.write_text('''function(database_force_link)
    if(TARGET aDataBase AND ADATABASE_BACKEND STREQUAL "CUSTOM")
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
        print(f"Database {name}: build/link passed", flush=True)
