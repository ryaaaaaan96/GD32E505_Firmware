#!/usr/bin/env python3
"""使用真实 FlashDB 和 aMemory 验证存储适配、KV 和时序数据库。"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
module = root / "func/aDataBase"
official = module / "FlashDB"
sources = [module / "aDataBase.c", module / "aDataBase_index.c",
           module / "port/memory_port.c",
           root / "tests/database/os_mock.c",
           root / "func/aMemory/aMemory.c"]
sources += [official / "src" / name for name in
            ("fdb.c", "fdb_kvdb.c", "fdb_tsdb.c", "fdb_utils.c")]

with tempfile.TemporaryDirectory(prefix="aclass-storage-") as directory:
    shell = Path(directory) / "aShell.h"
    shell.write_text('''#ifndef TEST_SHELL_H
#define TEST_SHELL_H
int test_shell_print(const char *format, ...);
#define ASHELL_PRINT(...) test_shell_print(__VA_ARGS__)
#define ASHELL_REPLY(...) ASHELL_PRINT(__VA_ARGS__)
#define ASHELL_CMD_EXPORT(n, f, h) \\
    int test_database_command(int argc, char **argv) \\
        { return f(argc, argv); } \\
    _Static_assert(1, "命令导出")
#endif
''')
    for static, dynamic in ((1, 0), (0, 1), (1, 1)):
        objects = []
        command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
                   "-Werror", "-fsanitize=undefined",
                   "-fno-sanitize-recover=all",
                   f"-DADATABASE_STATIC_ENABLE={static}",
                   f"-DADATABASE_DYNAMIC_ENABLE={dynamic}"]
        if os.environ.get("SANITIZE"):
            command += ["-fsanitize=address", "-fno-omit-frame-pointer"]
        for path in (module, module / "config", official / "inc",
                     root / "func/aMemory", root / "func/aBus/include",
                     root / "config",
                     root / "platform/aLib/include",
                     root / "platform/aOS/public"):
            command += ["-I", str(path)]
        for i, source in enumerate(sources):
            obj = str(Path(directory) / f"{static}-{dynamic}-{i}.o")
            flags = (["-Wno-unused-parameter"]
                     if official in source.parents else [])
            subprocess.run(command + flags + ["-c", str(source), "-o", obj],
                           check=True)
            objects.append(obj)
        for test in ("test_storage", "test_database", "test_multidevice",
                     "test_index"):
            executable = str(Path(directory) / f"{test}-{static}-{dynamic}")
            test_source = root / "tests/database" / (test + ".c")
            subprocess.run(command + [str(test_source)] + objects +
                           ["-o", executable], check=True)
            subprocess.run([executable], check=True, timeout=20)
        executable = str(Path(directory) / f"command-{static}-{dynamic}")
        subprocess.run(command + ["-DASHELL_ENABLE=1", "-I", directory,
                       "-I", str(root / "app/devices/system"),
                       "-I", str(root / "app/task/system"),
                       str(root / "tests/database/test_command.c"),
                       str(root / "app/devices/system/database_config.c"),
                       str(root / "app/task/system/database_service.c"),
                       str(root / "app/task/system/database_command.c")]
                       + objects + ["-o", executable], check=True)
        subprocess.run([executable], check=True, timeout=20)
