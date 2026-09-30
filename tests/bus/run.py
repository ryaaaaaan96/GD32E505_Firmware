#!/usr/bin/env python3
"""Check lock modes, definition validation, and real assert/NDEBUG behavior."""
from pathlib import Path
import itertools
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-bus-") as directory:
    for mode, checks, ndebug, allocation in itertools.product(
            (0, 1, 2), (0, 1), (0, 1), ((1, 0), (0, 1), (1, 1))):
        static, dynamic = allocation
        executable = str(Path(directory) / f"bus-{mode}-{checks}-{ndebug}-{static}-{dynamic}")
        obj = executable + ".o"
        flags = [
            "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
            "-Werror", "-pthread", "-ffunction-sections", "-fdata-sections", f"-DABUS_LOCK_MODE={mode}",
            f"-DABUS_DEF_CHECK_ENABLE={checks}",
            f"-DABUS_TEST_NDEBUG={ndebug}",
            f"-DABUS_STATIC_ENABLE={static}",
            f"-DABUS_DYNAMIC_ENABLE={dynamic}",
            "-Ifunc/aBus/include", "-Iplatform/aOS/public",
            "-Iplatform/aLib/include",
        ]
        if os.environ.get("SANITIZE"):
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(flags + (["-DNDEBUG"] if ndebug else []) + [
            "-c", "func/aBus/src/aBus.c", "-o", obj
        ], cwd=root, check=True)
        symbols = subprocess.check_output(["nm", "-u", obj], text=True)
        if not dynamic:
            assert "aOSAlloc" not in symbols and "aOSFree" not in symbols
        if mode == 0 and not dynamic:
            assert "aOS" not in symbols, symbols
        subprocess.run(flags + ["tests/bus/test_bus.c", "tests/bus/bindings.c", obj,
            "-Wl,--gc-sections",
            "-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld",
            "-o", executable],
                       cwd=root, check=True)
        subprocess.run([executable], check=True)
        print(f"aBus mode={mode} check={checks} NDEBUG={ndebug} "
              f"static={static} dynamic={dynamic} passed")

# Empty registry must also link and allocate all data dynamically.
with tempfile.TemporaryDirectory(prefix="aclass-bus-empty-") as directory:
    executable = str(Path(directory) / "empty")
    command = [
        "cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-DABUS_LOCK_MODE=0", "-DABUS_STATIC_ENABLE=0",
        "-DABUS_DYNAMIC_ENABLE=1", "-Ifunc/aBus/include",
        "-Iplatform/aOS/public", "-Iplatform/aLib/include",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld",
        "tests/bus/empty_bindings.c", "func/aBus/src/aBus.c",
        "-o", executable,
    ]
    if os.environ.get("SANITIZE"):
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([executable], check=True)
    print("aBus empty binding registry passed")
