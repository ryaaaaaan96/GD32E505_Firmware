#!/usr/bin/env python3
"""Check lock modes, definition validation, and real assert/NDEBUG behavior."""
from pathlib import Path
import itertools
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-bus-") as directory:
    for mode, checks, ndebug in itertools.product((0, 1, 2), (0, 1), (0, 1)):
        executable = str(Path(directory) / f"bus-{mode}-{checks}-{ndebug}")
        obj = executable + ".o"
        flags = [
            "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
            "-Werror", "-pthread", f"-DABUS_LOCK_MODE={mode}",
            f"-DABUS_DEF_CHECK_ENABLE={checks}",
            f"-DABUS_TEST_NDEBUG={ndebug}",
            "-Ifunc/aBus/include", "-Iplatform/aOS/public",
            "-Iplatform/aLib/include",
        ]
        if os.environ.get("SANITIZE"):
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(flags + (["-DNDEBUG"] if ndebug else []) + [
            "-c", "func/aBus/src/aBus.c", "-o", obj
        ], cwd=root, check=True)
        symbols = subprocess.check_output(["nm", "-u", obj], text=True)
        assert "aOSAlloc" not in symbols and "aOSFree" not in symbols
        if mode == 0:
            assert "aOS" not in symbols, symbols
        subprocess.run(flags + ["tests/bus/test_bus.c", obj, "-o", executable],
                       cwd=root, check=True)
        subprocess.run([executable], check=True)
        print(f"aBus mode={mode} def_check={checks} NDEBUG={ndebug} passed")
