#!/usr/bin/env python3
"""Build real firmware profiles in isolated directories; no source config edits."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
toolchain = os.environ.get("ARM_GCC_ROOT", str(Path.home() / "Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3"))
profiles = {
    "polling": (False, False, False, False),
    "interrupt": (True, False, False, False),
    "dma": (False, True, False, False),
    "async": (False, True, True, False),
    "rs485": (False, False, False, True),
    "full": (True, True, True, True),
}
with tempfile.TemporaryDirectory(prefix="aclass-matrix-") as directory:
    base = Path(directory)
    force_link = base / "force_device_link.cmake"
    force_link.write_text(
        'function(matrix_force_link)\n'
        '  get_target_property(libs "${PROJECT_NAME}" LINK_LIBRARIES)\n'
        '  list(REMOVE_ITEM libs aDevUsart)\n'
        '  set_property(TARGET "${PROJECT_NAME}" PROPERTY LINK_LIBRARIES "${libs}")\n'
        '  target_link_libraries("${PROJECT_NAME}" PRIVATE '
        '"-Wl,--whole-archive" aDevUsart "-Wl,--no-whole-archive")\n'
        'endfunction()\n'
        'cmake_language(DEFER CALL matrix_force_link)\n'
    )
    for name, values in profiles.items():
        config = base / (name + ".cmake")
        lines = [f'include("{root}/config/aclass_config.cmake")', "set(ASHELL_REQUESTED OFF)"]
        for feature, enabled in zip(("INTERRUPT", "DMA", "ASYNC", "RS485"), values):
            lines.append(f"set(ADEV_USART_{feature}_REQUESTED {'ON' if enabled else 'OFF'})")
        irq = any(values)
        dma = values[1]
        lines += [
            f"set(ADRV_USART_INTERRUPT_REQUESTED {'ON' if irq else 'OFF'})",
            f"set(ADRV_USART_ASYNC_REQUESTED {'ON' if dma else 'OFF'})",
            f"set(ADRV_MODULE_DMA_REQUESTED {'ON' if dma else 'OFF'})",
        ]
        config.write_text("\n".join(lines) + "\n")
        build = base / name
        commands = [
            ["cmake", "-S", str(root), "-B", str(build), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Debug", f"-DARM_GCC_ROOT={toolchain}",
             f"-DACLASS_CONFIG_FILE={config}", f"-DCMAKE_PROJECT_INCLUDE={force_link}"],
            ["cmake", "--build", str(build), "--parallel", "4"],
        ]
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                print(result.stdout, result.stderr)
                raise SystemExit(result.returncode)
        # Inspect the archive, not only the final ELF (which uses linker GC).
        nm = str(Path(toolchain) / "bin/arm-none-eabi-nm")
        symbols = subprocess.check_output([nm, str(build / "lib/libaDevUsart.a")], text=True)
        if not dma:
            assert "aDrvUsartAsync" not in symbols, name
            assert " T aDevUsartReadDirect" not in symbols, name
        if not irq:
            assert "aDrvUsartSetInterruptEnabled" not in symbols, name
            assert "aDrvUsartRegisterCallback" not in symbols, name
        if not values[2]:
            assert " T aDevUsartWriteAsync" not in symbols, name
            assert " T aDevUsartReadAsync" not in symbols, name
        if not values[3]:
            assert " T aDevUsartRS485Init" not in symbols, name
        print(f"{name}: build/link/archive checks passed")
