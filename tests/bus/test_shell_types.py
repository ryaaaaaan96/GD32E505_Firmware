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
    command = [
        "cc", "-std=c11", "-DABUS_EVENT_ENABLE=0", "-Wall", "-Wextra", "-Werror",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-DABUS_ENABLE=1", "-Iapp", "-Iapp/protocol/inc", "-Iapp/task/test",
        "-Iapp/devices/system", "-Idevice/aDev_LED", "-Iplatform/aDrv/include",
        "-DABUS_STATIC_ENABLE=1", "-DABUS_DYNAMIC_ENABLE=0",
        "-DABUS_LOCK_MODE=0", f"-I{tmp}", "-Iapp/task/system",
        "-Ifunc/aBus/include", "-Iplatform/aLib/include",
        "-Iplatform/aOS/public", "tests/bus/test_shell_types.c",
        "app/task/system/command_data_bus.c",
        "func/aBus/src/aBus.c",
        "-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld",
        "-o", executable,
    ]
    for reordered in (0, 1):
        subprocess.run(command + [f"-DTEST_REORDER={reordered}"],
                       cwd=root, check=True)
        subprocess.run([executable], check=True)
        print(f"aBus key-based shell passed: reordered={reordered}")
