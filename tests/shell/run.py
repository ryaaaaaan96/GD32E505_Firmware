#!/usr/bin/env python3
"""GCC section registration, real parser, lifecycle and stream regressions."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
common = [
    "gcc", "-std=c11", "-pthread", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-Ifunc/aShell/include", "-Iplatform/aLib/include",
]
enabled = [
    "-DASHELL_ENABLE=1", "-fno-pie", "-no-pie",
    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
    "-Wl,--fatal-warnings",
    "-Wl,-T,func/aShell/port/gcc/aShell_sections_host.ld",
    "-Ifunc/aShell/nr_micro_shell/inc", "-Ifunc/aShell/port",
    "-Ifunc/aShell", "-Iplatform/aOS/public",
    "tests/shell/test_shell.c", "func/aShell/src/aShell.c",
    "func/aShell/src/aShell_output.c",
    "func/aShell/src/aShell_nr.c", "func/aShell/src/aShell_config.c",
    "func/aShell/nr_micro_shell/src/nr_micro_shell_core.c",
]
with tempfile.TemporaryDirectory(prefix="aclass-shell-") as directory:
    for name, flags in (
        ("parser", []),
        ("optimized_lto", ["-O2", "-flto"]),
        ("duplicate", ["-DTEST_DUPLICATE", "tests/shell/test_duplicate.c"]),
    ):
        executable = str(Path(directory) / name)
        command = common + enabled + flags + ["-o", executable]
        if os.environ.get("SANITIZE"):
            command[1:1] = [
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            ]
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([executable], check=True)
        print(name, "passed with section GC enabled")

    stub = str(Path(directory) / "shell_stub")
    subprocess.run(common + [
        "-DASHELL_ENABLE=0", "-ffunction-sections", "-fdata-sections",
        "-Wl,--gc-sections", "tests/shell/test_stub.c",
        "func/aShell/src/aShell_stub.c", "func/aShell/src/aShell_config.c",
        "-o", stub,
    ], cwd=root, check=True)
    subprocess.run([stub], check=True)
    symbols = subprocess.check_output(["nm", stub], text=True)
    assert "disabled_command" not in symbols
    assert "cmd_table" not in symbols
    print("Disabled exports and Print skip symbols and argument evaluation")
