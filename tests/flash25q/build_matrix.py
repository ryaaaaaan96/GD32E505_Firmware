#!/usr/bin/env python3
"""Build SPI Flash profiles without altering the product configuration."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
toolchain = os.environ.get(
    "ARM_GCC_ROOT",
    str(Path.home() / "Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3"))
profiles = {
    "static": "set(ADEV_FLASH25Q_DYNAMIC_ENABLE OFF)",
    "dynamic": "set(ADEV_FLASH25Q_STATIC_ENABLE OFF)",
    "disabled": "set(ADEV_FLASH25Q_ENABLE OFF)\n"
                "set(ADRV_MODULE_SPI_ENABLE OFF)",
    "database": "set(ADATABASE_ENABLE ON)\n"
                "set(ADATABASE_BACKEND FLASH25Q)",
}
with tempfile.TemporaryDirectory(prefix="flash25q-build-") as directory:
    base = Path(directory)
    force_link = base / "force.cmake"
    force_link.write_text('''function(flash_force_link)
    if(TARGET aDataBaseFlash25q)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "-Wl,--whole-archive" aDataBaseFlash25q "-Wl,--no-whole-archive")
    endif()
endfunction()
cmake_language(DEFER CALL flash_force_link)
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
        print(f"Flash25Q {name}: build/link passed", flush=True)
