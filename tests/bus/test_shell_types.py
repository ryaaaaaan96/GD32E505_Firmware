#!/usr/bin/env python3
"""Verify table-driven scalar/RAW/STRUCT commands against a separate table."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / "aShell.h").write_text('''
int test_print(const char *format, ...);
#define ASHELL_PRINT(...) test_print(__VA_ARGS__)
#define ASHELL_REPLY(...) ASHELL_PRINT(__VA_ARGS__)
#define ASHELL_CMD_EXPORT(name, fn, desc) \\
int test_command(int argc, char **argv) { return fn(argc, argv); }
''')
    executable = str(tmp / "shell-types")
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-DABUS_STATIC_ENABLE=1", "-DABUS_DYNAMIC_ENABLE=0",
        "-DABUS_LOCK_MODE=0", f"-I{tmp}", "-Iapp/data/sig",
        "-Ifunc/aBus/include", "-Iplatform/aLib/include",
        "-Iplatform/aOS/public", "tests/bus/test_shell_types.c",
        "app/data/sig/sig_command.c", "func/aBus/src/aBus.c",
        "-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld",
        "-o", executable,
    ], cwd=root, check=True)
    subprocess.run([executable], check=True)
    print("aBus table-driven shell types passed")
